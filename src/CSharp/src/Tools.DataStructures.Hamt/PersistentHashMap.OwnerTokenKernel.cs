using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Threading;

namespace Tools.DataStructures.Hamt;

public sealed partial class PersistentHashMap<TKey, TValue>
{
    /// <summary>
    /// A unique, one-way edit capability. Nodes retain the token after publication; sealing it
    /// makes every node carrying it permanently immutable without a graph walk.
    /// </summary>
    internal sealed class EditToken
    {
        private int _sealed;

        internal bool IsActive => Volatile.Read(ref _sealed) == 0;

        internal void Seal() => Interlocked.Exchange(ref _sealed, 1);
    }

    internal enum OwnerTokenKernelFailurePoint
    {
        BeforeTokenAllocation,
        BeforeCommitPlanAllocation,
        BeforeNodeAllocation,
        BeforeDataArrayAllocation,
        BeforeChildArrayAllocation,
        BeforeCollisionArrayAllocation,
        MutationPrepared,
        BeforePublicationAllocation,
        PublicationPrepared,
    }

    internal readonly record struct OwnerTokenKernelCounters(
        long AdoptionCount,
        long AdoptionNodeVisits,
        long PublicationCount,
        long PublicationNodeVisits,
        long DeferredPersistentMutationCount,
        long EditablePromotionCount,
        long CommitPlanAllocationCount,
        long PreparedMutationCount,
        long CopiedNodeCount,
        long AllocatedNodeCount,
        long CopiedArrayCount,
        long AllocatedArrayCount,
        long InPlaceNodeMutationCount,
        long InPlaceArrayWriteCount,
        long PersistentWrapperAllocationCount,
        long DeferredPersistentWrapperAllocationCount)
    {
        public static OwnerTokenKernelCounters operator +(
            OwnerTokenKernelCounters left,
            OwnerTokenKernelCounters right) =>
            new(
                left.AdoptionCount + right.AdoptionCount,
                left.AdoptionNodeVisits + right.AdoptionNodeVisits,
                left.PublicationCount + right.PublicationCount,
                left.PublicationNodeVisits + right.PublicationNodeVisits,
                left.DeferredPersistentMutationCount + right.DeferredPersistentMutationCount,
                left.EditablePromotionCount + right.EditablePromotionCount,
                left.CommitPlanAllocationCount + right.CommitPlanAllocationCount,
                left.PreparedMutationCount + right.PreparedMutationCount,
                left.CopiedNodeCount + right.CopiedNodeCount,
                left.AllocatedNodeCount + right.AllocatedNodeCount,
                left.CopiedArrayCount + right.CopiedArrayCount,
                left.AllocatedArrayCount + right.AllocatedArrayCount,
                left.InPlaceNodeMutationCount + right.InPlaceNodeMutationCount,
                left.InPlaceArrayWriteCount + right.InPlaceArrayWriteCount,
                left.PersistentWrapperAllocationCount + right.PersistentWrapperAllocationCount,
                left.DeferredPersistentWrapperAllocationCount + right.DeferredPersistentWrapperAllocationCount);
    }

    /// <summary>
    /// Creates the private T1 kernel whose editable branch and collision nodes are separate from
    /// the owner-free ordinary persistent-node classes. This is an evidence seam, not the proposed
    /// public transient API.
    /// </summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal OwnerTokenTransientKernel CreateSeparateNodeTransientKernel(
        bool enableDiagnostics = true,
        bool deferOwnershipUntilReuse = true) =>
        new(this, enableDiagnostics, deferOwnershipUntilReuse);

    /// <summary>
    /// Private production-layout experiment for one-way CHAMP mutation. The session is deliberately
    /// absent from the public API until the T1 evidence gate is decided.
    /// </summary>
    internal sealed class OwnerTokenTransientKernel
    {
        private const int ActiveState = 1;
        private const int InactiveState = 0;
        private const int MaximumCommitSteps = 16;

        private PersistentHashMap<TKey, TValue>? _source;
        private Node? _root;
        private readonly IEqualityComparer<TKey> _comparer;
        private readonly bool _deferOwnershipUntilReuse;
        private readonly bool _diagnosticsEnabled;
        private EditToken? _token;
        private CommitStep[]? _commits;
        private KernelDiagnostics? _diagnostics;
        private int _commitCount;
        private int _count;
        private int _version;
        private int _state = ActiveState;
        private bool _useProductionFirstEditFastPath;
        private bool _hasPersistentMutation;
        private bool _dirty;

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        internal OwnerTokenTransientKernel(
            PersistentHashMap<TKey, TValue> source,
            bool enableDiagnostics,
            bool deferOwnershipUntilReuse)
        {
            _source = source;
            _root = source._root;
            _count = source._count;
            _comparer = source._comparer;
            _deferOwnershipUntilReuse = deferOwnershipUntilReuse;
            _diagnosticsEnabled = enableDiagnostics;
            _useProductionFirstEditFastPath = !enableDiagnostics && deferOwnershipUntilReuse;
            if (enableDiagnostics)
            {
                _diagnostics = new KernelDiagnostics();
                _diagnostics.Counters.AdoptionCount = 1;
                _diagnostics.Counters.AdoptionNodeVisits = 0;
            }
        }

        internal Action<OwnerTokenKernelFailurePoint>? FailureInjector
        {
            get => _diagnostics?.FailureInjector;
            set
            {
                if (!_diagnosticsEnabled)
                {
                    if (value is not null)
                    {
                        throw new InvalidOperationException(
                            "Failure injection is unavailable when kernel diagnostics are disabled.");
                    }
                    return;
                }

                if (value is null)
                {
                    _diagnostics!.FailureInjector = null;
                    return;
                }

                _diagnostics!.FailureInjector = value;
            }
        }

        internal int Count
        {
            get
            {
                EnsureActive();
                return _count;
            }
        }

        internal IEqualityComparer<TKey> Comparer
        {
            get
            {
                EnsureActive();
                return _comparer;
            }
        }

        internal bool IsActiveForDiagnostics => Volatile.Read(ref _state) == ActiveState;

        internal bool TokenIsActiveForDiagnostics =>
            IsActiveForDiagnostics && (_token?.IsActive ?? true);

        internal bool TokenIsAllocatedForDiagnostics => _token is not null;

        internal bool CommitPlanIsAllocatedForDiagnostics => _commits is not null;

        internal int VersionForDiagnostics => _version;

        internal object? RootIdentityForDiagnostics => _root;

        internal PersistentHashMap<TKey, TValue>? DeferredPersistentIdentityForDiagnostics =>
            _hasPersistentMutation && !_dirty ? _source : null;

        internal OwnerTokenKernelCounters GetCountersForDiagnostics()
        {
            if (!_diagnosticsEnabled)
            {
                throw new InvalidOperationException(
                    "Counters are unavailable because kernel diagnostics were disabled at adoption.");
            }

            return _diagnostics!.Counters.Snapshot();
        }

        internal bool ContainsKey(TKey key) => TryGetValue(key, out _);

        internal bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value)
        {
            EnsureActive();
            return TryGetEntry(_root, key, GetHash(key), _comparer, out _, out value);
        }

        internal bool TryGetKey(TKey key, out TKey actualKey)
        {
            EnsureActive();
            if (TryGetEntry(_root, key, GetHash(key), _comparer, out actualKey, out _))
                return true;

            actualKey = key;
            return false;
        }

        internal KeyValuePair<TKey, TValue>[] ToArrayForDiagnostics()
        {
            EnsureActive();
            var entries = new KeyValuePair<TKey, TValue>[_count];
            var index = 0;
            CopyEntries(_root, entries, ref index);
            if (index != entries.Length)
                throw new InvalidOperationException("The transient root count does not match its entries.");
            return entries;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        internal void SetItem(TKey key, TValue value)
        {
            if (_useProductionFirstEditFastPath)
            {
                SetFirstPersistentFast(key, value);
                return;
            }

            SetCore(key, value, overwrite: true);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        internal bool TryAdd(TKey key, TValue value)
        {
            if (_useProductionFirstEditFastPath)
            {
                return TryAddFirstPersistentFast(key, value);
            }

            return SetCore(key, value, overwrite: false).Added;
        }

        internal void Add(TKey key, TValue value)
        {
            if (!TryAdd(key, value))
                throw new ArgumentException($"An equivalent key '{key}' is already present.", nameof(key));
        }

        internal bool Remove(TKey key)
        {
            EnsureActive();
            if (_root is null)
                return false;

            if (_useProductionFirstEditFastPath)
                return RemoveFirstPersistentFast(key);

            if (_deferOwnershipUntilReuse && !_hasPersistentMutation && !_dirty)
            {
                return RemoveFirstPersistent(key);
            }

            var countersBefore = CaptureCounters();
            var tokenBefore = _token;
            var commitPlanBefore = _commits;
            var promotesEditablePath = _hasPersistentMutation && !_dirty;
            var newVersion = checked(_version + 1);
            _commitCount = 0;
            try
            {
                var prepared = PrepareRemove(_root, key, GetHash(key), shift: 0);
                if (!prepared.Removed)
                    return false;

                var newCount = checked(_count - 1);
                Hit(OwnerTokenKernelFailurePoint.MutationPrepared);
                ApplyCommits();
                _root = prepared.Node;
                _count = newCount;
                _version = newVersion;
                _dirty = true;
                _source = null;
                if (_diagnostics is { } diagnostics)
                {
                    diagnostics.Counters.PreparedMutationCount++;
                    if (promotesEditablePath && tokenBefore is null && _token is not null)
                        diagnostics.Counters.EditablePromotionCount++;
                }
                return true;
            }
            catch
            {
                RestoreCounters(countersBefore);
                RestoreTransientResources(tokenBefore, commitPlanBefore);
                throw;
            }
            finally
            {
                ClearCommitPlan();
            }
        }

        internal void Clear()
        {
            EnsureActive();
            if (_root is null)
                return;

            if (_deferOwnershipUntilReuse && !_hasPersistentMutation && !_dirty)
            {
                var newVersion = checked(_version + 1);
                var result = _source!.Clear();
                Hit(OwnerTokenKernelFailurePoint.MutationPrepared);
                _source = result;
                _root = result._root;
                _count = result._count;
                _useProductionFirstEditFastPath = false;
                _hasPersistentMutation = true;
                _version = newVersion;
                if (_diagnostics is { } firstDiagnostics)
                {
                    firstDiagnostics.Counters.DeferredPersistentMutationCount++;
                    firstDiagnostics.Counters.PreparedMutationCount++;
                    if (!ReferenceEquals(result, Empty))
                    {
                        firstDiagnostics.Counters.PersistentWrapperAllocationCount++;
                        firstDiagnostics.Counters.DeferredPersistentWrapperAllocationCount++;
                    }
                }
                return;
            }

            _version = checked(_version + 1);
            _root = null;
            _count = 0;
            _dirty = true;
            _source = null;
            if (_diagnostics is { } diagnostics)
                diagnostics.Counters.PreparedMutationCount++;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        internal PersistentHashMap<TKey, TValue> Persist()
        {
            EnsureActive();
            if (_diagnostics is null && !_dirty)
                return PersistOrdinaryFast();

            return PersistPrepared();
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        private PersistentHashMap<TKey, TValue> PersistPrepared()
        {
            var countersBefore = CaptureCounters();
            var newVersion = checked(_version + 1);
            try
            {
                PersistentHashMap<TKey, TValue> result;
                if (!_dirty)
                {
                    result = _source!;
                }
                else if (_count == 0
                    && ReferenceEquals(_comparer, EqualityComparer<TKey>.Default))
                {
                    // The default-policy empty is already allocated. Publication still reaches
                    // PublicationPrepared below, but there is no allocation boundary to inject.
                    result = Empty;
                }
                else
                {
                    Hit(OwnerTokenKernelFailurePoint.BeforePublicationAllocation);
                    result = _count == 0
                        ? EmptyFor(_comparer)
                        : new PersistentHashMap<TKey, TValue>(_root, _count, _comparer);
                    if (_diagnostics is { } allocationDiagnostics)
                        allocationDiagnostics.Counters.PersistentWrapperAllocationCount++;
                }

                Hit(OwnerTokenKernelFailurePoint.PublicationPrepared);

                // From this point onward publication consists only of non-throwing state changes.
                _token?.Seal();
                _version = newVersion;
                Volatile.Write(ref _state, InactiveState);
                _root = null;
                _source = null;
                if (_diagnostics is { } diagnostics)
                {
                    diagnostics.Counters.PublicationCount++;
                    diagnostics.Counters.PublicationNodeVisits = 0;
                }
                return result;
            }
            catch
            {
                RestoreCounters(countersBefore);
                throw;
            }
        }

        private SetPreparation SetCore(TKey key, TValue value, bool overwrite)
        {
            EnsureActive();
            if (_deferOwnershipUntilReuse && !_hasPersistentMutation && !_dirty)
                return SetFirstPersistent(key, value, overwrite);

            var countersBefore = CaptureCounters();
            var tokenBefore = _token;
            var commitPlanBefore = _commits;
            var promotesEditablePath = _hasPersistentMutation && !_dirty;
            var newVersion = checked(_version + 1);
            _commitCount = 0;
            try
            {
                var hash = GetHash(key);
                var prepared = _root is null
                    ? new SetPreparation(AllocateLeaf(hash, key, value), Changed: true, Added: true, CountDelta: 1)
                    : PrepareSet(_root, key, value, hash, shift: 0, overwrite);
                if (!prepared.Changed)
                    return prepared;

                var newCount = checked(_count + prepared.CountDelta);
                Hit(OwnerTokenKernelFailurePoint.MutationPrepared);
                ApplyCommits();
                _root = prepared.Node;
                _count = newCount;
                _version = newVersion;
                _dirty = true;
                _source = null;
                if (_diagnostics is { } diagnostics)
                {
                    diagnostics.Counters.PreparedMutationCount++;
                    if (promotesEditablePath && tokenBefore is null && _token is not null)
                        diagnostics.Counters.EditablePromotionCount++;
                }
                return prepared;
            }
            catch
            {
                RestoreCounters(countersBefore);
                RestoreTransientResources(tokenBefore, commitPlanBefore);
                throw;
            }
            finally
            {
                ClearCommitPlan();
            }
        }

        private bool RemoveFirstPersistent(TKey key)
        {
            var countersBefore = CaptureCounters();
            var newVersion = checked(_version + 1);
            try
            {
                var result = _source!.Remove(key);
                if (ReferenceEquals(result, _source))
                    return false;

                Hit(OwnerTokenKernelFailurePoint.MutationPrepared);
                _source = result;
                _root = result._root;
                _count = result._count;
                _hasPersistentMutation = true;
                _version = newVersion;
                if (_diagnostics is { } diagnostics)
                {
                    diagnostics.Counters.DeferredPersistentMutationCount++;
                    diagnostics.Counters.PreparedMutationCount++;
                    if (!ReferenceEquals(result, Empty))
                    {
                        diagnostics.Counters.PersistentWrapperAllocationCount++;
                        diagnostics.Counters.DeferredPersistentWrapperAllocationCount++;
                    }
                }
                return true;
            }
            catch
            {
                RestoreCounters(countersBefore);
                throw;
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private void SetFirstPersistentFast(TKey key, TValue value)
        {
            EnsureActive();
            var newVersion = checked(_version + 1);
            var result = _source!.SetItem(key, value);
            if (ReferenceEquals(result, _source))
                return;

            _source = result;
            _root = result._root;
            _count = result._count;
            _useProductionFirstEditFastPath = false;
            _hasPersistentMutation = true;
            _version = newVersion;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private bool TryAddFirstPersistentFast(TKey key, TValue value)
        {
            EnsureActive();
            var newVersion = checked(_version + 1);
            if (!_source!.TryAdd(key, value, out var result))
                return false;

            _source = result;
            _root = result._root;
            _count = result._count;
            _useProductionFirstEditFastPath = false;
            _hasPersistentMutation = true;
            _version = newVersion;
            return true;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private bool RemoveFirstPersistentFast(TKey key)
        {
            var newVersion = checked(_version + 1);
            var result = _source!.Remove(key);
            if (ReferenceEquals(result, _source))
                return false;

            _source = result;
            _root = result._root;
            _count = result._count;
            _useProductionFirstEditFastPath = false;
            _hasPersistentMutation = true;
            _version = newVersion;
            return true;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private PersistentHashMap<TKey, TValue> PersistOrdinaryFast()
        {
            var result = _source!;
            var newVersion = checked(_version + 1);
            Debug.Assert(_token is null, "Owner-free publication cannot have allocated an edit token.");
            _version = newVersion;
            Volatile.Write(ref _state, InactiveState);
            _root = null;
            _source = null;
            return result;
        }

        private SetPreparation SetFirstPersistent(TKey key, TValue value, bool overwrite)
        {
            var countersBefore = CaptureCounters();
            var newVersion = checked(_version + 1);
            try
            {
                PersistentHashMap<TKey, TValue> result;
                bool added;
                if (overwrite)
                {
                    result = _source!.SetItem(key, value);
                    if (ReferenceEquals(result, _source))
                        return new SetPreparation(_root!, Changed: false, Added: false, CountDelta: 0);
                    added = result._count != _count;
                }
                else
                {
                    added = _source!.TryAdd(key, value, out result);
                    if (!added)
                        return new SetPreparation(_root!, Changed: false, Added: false, CountDelta: 0);
                }

                var countDelta = added ? 1 : 0;
                Hit(OwnerTokenKernelFailurePoint.MutationPrepared);
                _source = result;
                _root = result._root;
                _count = result._count;
                _hasPersistentMutation = true;
                _version = newVersion;
                if (_diagnostics is { } diagnostics)
                {
                    diagnostics.Counters.DeferredPersistentMutationCount++;
                    diagnostics.Counters.PreparedMutationCount++;
                    diagnostics.Counters.PersistentWrapperAllocationCount++;
                    diagnostics.Counters.DeferredPersistentWrapperAllocationCount++;
                }
                return new SetPreparation(result._root!, Changed: true, added, countDelta);
            }
            catch
            {
                RestoreCounters(countersBefore);
                throw;
            }
        }

        private uint GetHash(TKey key) => unchecked((uint)_comparer.GetHashCode(key!));

        private static bool TryGetEntry(
            Node? root,
            TKey key,
            uint hash,
            IEqualityComparer<TKey> comparer,
            out TKey actualKey,
            [MaybeNullWhen(false)] out TValue value)
        {
            var node = root;
            var shift = 0;
            while (node is not null)
            {
                if (node is LeafNode leaf)
                {
                    if (leaf.Hash == hash && comparer.Equals(leaf.Key, key))
                    {
                        actualKey = leaf.Key;
                        value = leaf.Value;
                        return true;
                    }

                    actualKey = key;
                    value = default;
                    return false;
                }

                Entry[]? collisionEntries = null;
                uint collisionHash = 0;
                if (node is CollisionNode ordinaryCollision)
                {
                    collisionHash = ordinaryCollision.Hash;
                    collisionEntries = ordinaryCollision.Entries;
                }
                else if (node is SeparateTransientCollisionNode separateCollision)
                {
                    collisionHash = separateCollision.Hash;
                    collisionEntries = separateCollision.Entries;
                }

                if (collisionEntries is not null)
                {
                    if (collisionHash == hash)
                    {
                        foreach (var entry in collisionEntries)
                        {
                            if (!comparer.Equals(entry.Key, key))
                                continue;
                            actualKey = entry.Key;
                            value = entry.Value;
                            return true;
                        }
                    }

                    actualKey = key;
                    value = default;
                    return false;
                }

                uint dataMap;
                Entry[] data;
                uint nodeMap;
                Node[] children;
                if (node is BitmapIndexedNode ordinaryBranch)
                {
                    dataMap = ordinaryBranch.DataMap;
                    data = ordinaryBranch.Data;
                    nodeMap = ordinaryBranch.NodeMap;
                    children = ordinaryBranch.Children;
                }
                else
                {
                    var separateBranch = (SeparateTransientBranchNode)node;
                    dataMap = separateBranch.DataMap;
                    data = separateBranch.Data;
                    nodeMap = separateBranch.NodeMap;
                    children = separateBranch.Children;
                }

                var bit = Bit(Index(hash, shift));
                if ((dataMap & bit) != 0)
                {
                    var entry = data[Slot(dataMap, bit)];
                    if (entry.Hash == hash && comparer.Equals(entry.Key, key))
                    {
                        actualKey = entry.Key;
                        value = entry.Value;
                        return true;
                    }

                    actualKey = key;
                    value = default;
                    return false;
                }

                if ((nodeMap & bit) == 0)
                {
                    actualKey = key;
                    value = default;
                    return false;
                }

                node = children[Slot(nodeMap, bit)];
                shift += BitsPerLevel;
            }

            actualKey = key;
            value = default;
            return false;
        }

        private static void CopyEntries(
            Node? node,
            KeyValuePair<TKey, TValue>[] destination,
            ref int index)
        {
            switch (node)
            {
                case null:
                    return;
                case LeafNode leaf:
                    destination[index++] = KeyValuePair.Create(leaf.Key, leaf.Value);
                    return;
                case CollisionNode collision:
                    foreach (var entry in collision.Entries)
                        destination[index++] = KeyValuePair.Create(entry.Key, entry.Value);
                    return;
                case SeparateTransientCollisionNode collision:
                    foreach (var entry in collision.Entries)
                        destination[index++] = KeyValuePair.Create(entry.Key, entry.Value);
                    return;
                case BitmapIndexedNode branch:
                    foreach (var entry in branch.Data)
                        destination[index++] = KeyValuePair.Create(entry.Key, entry.Value);
                    foreach (var child in branch.Children)
                        CopyEntries(child, destination, ref index);
                    return;
                case SeparateTransientBranchNode branch:
                    foreach (var entry in branch.Data)
                        destination[index++] = KeyValuePair.Create(entry.Key, entry.Value);
                    foreach (var child in branch.Children)
                        CopyEntries(child, destination, ref index);
                    return;
            }
        }

        private SetPreparation PrepareSet(
            Node node,
            TKey key,
            TValue value,
            uint hash,
            int shift,
            bool overwrite) => node switch
        {
            LeafNode leaf => PrepareLeafSet(leaf, key, value, hash, shift, overwrite),
            CollisionNode collision =>
                PrepareCollisionSet(collision, collision.Entries, key, value, hash, shift, overwrite),
            SeparateTransientCollisionNode collision =>
                PrepareCollisionSet(collision, collision.Entries, key, value, hash, shift, overwrite),
            BitmapIndexedNode branch =>
                PrepareBranchSet(BranchView.Create(branch), key, value, hash, shift, overwrite),
            SeparateTransientBranchNode branch =>
                PrepareBranchSet(BranchView.Create(branch), key, value, hash, shift, overwrite),
            _ => throw new InvalidOperationException("Unknown CHAMP node kind."),
        };

        private SetPreparation PrepareLeafSet(
            LeafNode leaf,
            TKey key,
            TValue value,
            uint hash,
            int shift,
            bool overwrite)
        {
            if (leaf.Hash == hash && _comparer.Equals(leaf.Key, key))
            {
                if (!overwrite || ValuesEqual(leaf.Value, value))
                    return new SetPreparation(leaf, Changed: false, Added: false, CountDelta: 0);
                return new SetPreparation(
                    AllocateLeaf(hash, leaf.Key, value),
                    Changed: true,
                    Added: false,
                    CountDelta: 0);
            }

            var right = AllocateLeaf(hash, key, value);
            return new SetPreparation(
                MergeHashNodesTransient(leaf, right, shift),
                Changed: true,
                Added: true,
                CountDelta: 1);
        }

        private SetPreparation PrepareCollisionSet(
            HashNode collision,
            Entry[] collisionEntries,
            TKey key,
            TValue value,
            uint hash,
            int shift,
            bool overwrite)
        {
            if (collision.Hash != hash)
            {
                var right = AllocateLeaf(hash, key, value);
                return new SetPreparation(
                    MergeHashNodesTransient(collision, right, shift),
                    Changed: true,
                    Added: true,
                    CountDelta: 1);
            }

            for (var index = 0; index < collisionEntries.Length; index++)
            {
                var stored = collisionEntries[index];
                if (!_comparer.Equals(stored.Key, key))
                    continue;
                if (!overwrite || ValuesEqual(stored.Value, value))
                    return new SetPreparation(collision, Changed: false, Added: false, CountDelta: 0);

                var replacement = new Entry(hash, stored.Key, value);
                if (CanWriteEntries(collision))
                {
                    var result = FinishCollision(
                        collision,
                        collisionEntries,
                        writeEntry: true,
                        entryIndex: index,
                        entry: replacement);
                    return new SetPreparation(result, Changed: true, Added: false, CountDelta: 0);
                }

                var entries = AllocateCollisionArray(collisionEntries.Length, copiesExisting: true);
                Array.Copy(collisionEntries, entries, entries.Length);
                entries[index] = replacement;
                return new SetPreparation(
                    FinishCollision(collision, entries),
                    Changed: true,
                    Added: false,
                    CountDelta: 0);
            }

            var appended = AllocateCollisionArray(collisionEntries.Length + 1, copiesExisting: true);
            Array.Copy(collisionEntries, appended, collisionEntries.Length);
            appended[^1] = new Entry(hash, key, value);
            return new SetPreparation(
                FinishCollision(collision, appended),
                Changed: true,
                Added: true,
                CountDelta: 1);
        }

        private SetPreparation PrepareBranchSet(
            BranchView branch,
            TKey key,
            TValue value,
            uint hash,
            int shift,
            bool overwrite)
        {
            var bit = Bit(Index(hash, shift));
            if ((branch.DataMap & bit) != 0)
            {
                var slot = Slot(branch.DataMap, bit);
                var stored = branch.Data[slot];
                if (stored.Hash == hash && _comparer.Equals(stored.Key, key))
                {
                    if (!overwrite || ValuesEqual(stored.Value, value))
                        return new SetPreparation(branch.Source, Changed: false, Added: false, CountDelta: 0);

                    var replacement = new Entry(hash, stored.Key, value);
                    if (CanWriteData(branch))
                    {
                        var writableState = StateOf(branch, branch.Count);
                        var result = FinishBranch(
                            branch,
                            writableState,
                            writeData: true,
                            dataIndex: slot,
                            dataEntry: replacement);
                        return new SetPreparation(result, Changed: true, Added: false, CountDelta: 0);
                    }

                    var data = CloneData(branch.Data);
                    data[slot] = replacement;
                    var replacedState = StateOf(branch, branch.Count) with
                    {
                        Data = data,
                        DataOwned = true,
                    };
                    return new SetPreparation(
                        FinishBranch(branch, replacedState),
                        Changed: true,
                        Added: false,
                        CountDelta: 0);
                }

                var left = AllocateLeaf(stored.Hash, stored.Key, stored.Value);
                var right = AllocateLeaf(hash, key, value);
                var child = MergeHashNodesTransient(left, right, shift + BitsPerLevel);
                var dataWithoutStored = RemoveData(branch.Data, slot);
                var childSlot = Slot(branch.NodeMap, bit);
                var children = InsertChild(branch.Children, childSlot, child);
                var state = new BranchState(
                    branch.DataMap ^ bit,
                    dataWithoutStored,
                    true,
                    branch.NodeMap | bit,
                    children,
                    true,
                    checked(branch.Count + 1));
                return new SetPreparation(
                    FinishBranch(branch, state),
                    Changed: true,
                    Added: true,
                    CountDelta: 1);
            }

            if ((branch.NodeMap & bit) != 0)
            {
                var slot = Slot(branch.NodeMap, bit);
                var oldChild = branch.Children[slot];
                var prepared = PrepareSet(oldChild, key, value, hash, shift + BitsPerLevel, overwrite);
                if (!prepared.Changed)
                    return new SetPreparation(branch.Source, Changed: false, Added: prepared.Added, CountDelta: 0);

                var count = checked(branch.Count + prepared.CountDelta);
                if (ReferenceEquals(prepared.Node, oldChild))
                {
                    if (prepared.CountDelta == 0)
                        return new SetPreparation(branch.Source, Changed: true, Added: prepared.Added, CountDelta: 0);
                    if (!IsOwned(branch))
                        throw new InvalidOperationException("An active child cannot occur beneath an unowned branch.");
                    var countState = StateOf(branch, count);
                    return new SetPreparation(
                        FinishBranch(branch, countState),
                        Changed: true,
                        Added: prepared.Added,
                        CountDelta: prepared.CountDelta);
                }

                if (CanWriteChildren(branch))
                {
                    var state = StateOf(branch, count);
                    var result = FinishBranch(
                        branch,
                        state,
                        writeChild: true,
                        childIndex: slot,
                        child: prepared.Node);
                    return new SetPreparation(
                        result,
                        Changed: true,
                        Added: prepared.Added,
                        CountDelta: prepared.CountDelta);
                }

                var children = CloneChildren(branch.Children);
                children[slot] = prepared.Node;
                var replacedState = StateOf(branch, count) with
                {
                    Children = children,
                    ChildrenOwned = true,
                };
                return new SetPreparation(
                    FinishBranch(branch, replacedState),
                    Changed: true,
                    Added: prepared.Added,
                    CountDelta: prepared.CountDelta);
            }

            var inserted = InsertData(
                branch.Data,
                Slot(branch.DataMap, bit),
                new Entry(hash, key, value));
            var insertedState = StateOf(branch, checked(branch.Count + 1)) with
            {
                DataMap = branch.DataMap | bit,
                Data = inserted,
                DataOwned = true,
            };
            return new SetPreparation(
                FinishBranch(branch, insertedState),
                Changed: true,
                Added: true,
                CountDelta: 1);
        }

        private RemovePreparation PrepareRemove(Node node, TKey key, uint hash, int shift) => node switch
        {
            LeafNode leaf => PrepareLeafRemove(leaf, key, hash),
            CollisionNode collision => PrepareCollisionRemove(collision, collision.Entries, key, hash),
            SeparateTransientCollisionNode collision =>
                PrepareCollisionRemove(collision, collision.Entries, key, hash),
            BitmapIndexedNode branch => PrepareBranchRemove(BranchView.Create(branch), key, hash, shift),
            SeparateTransientBranchNode branch =>
                PrepareBranchRemove(BranchView.Create(branch), key, hash, shift),
            _ => throw new InvalidOperationException("Unknown CHAMP node kind."),
        };

        private RemovePreparation PrepareLeafRemove(LeafNode leaf, TKey key, uint hash)
        {
            var removed = leaf.Hash == hash && _comparer.Equals(leaf.Key, key);
            return new RemovePreparation(removed ? null : leaf, removed);
        }

        private RemovePreparation PrepareCollisionRemove(
            HashNode collision,
            Entry[] collisionEntries,
            TKey key,
            uint hash)
        {
            if (collision.Hash != hash)
                return new RemovePreparation(collision, Removed: false);

            for (var index = 0; index < collisionEntries.Length; index++)
            {
                if (!_comparer.Equals(collisionEntries[index].Key, key))
                    continue;
                if (collisionEntries.Length == 2)
                {
                    var remaining = collisionEntries[1 - index];
                    return new RemovePreparation(
                        AllocateLeaf(remaining.Hash, remaining.Key, remaining.Value),
                        Removed: true);
                }

                var entries = AllocateCollisionArray(collisionEntries.Length - 1, copiesExisting: true);
                if (index > 0)
                    Array.Copy(collisionEntries, 0, entries, 0, index);
                if (index < collisionEntries.Length - 1)
                {
                    Array.Copy(
                        collisionEntries,
                        index + 1,
                        entries,
                        index,
                        collisionEntries.Length - index - 1);
                }
                return new RemovePreparation(FinishCollision(collision, entries), Removed: true);
            }

            return new RemovePreparation(collision, Removed: false);
        }

        private RemovePreparation PrepareBranchRemove(
            BranchView branch,
            TKey key,
            uint hash,
            int shift)
        {
            var bit = Bit(Index(hash, shift));
            if ((branch.DataMap & bit) != 0)
            {
                var slot = Slot(branch.DataMap, bit);
                var entry = branch.Data[slot];
                if (entry.Hash != hash || !_comparer.Equals(entry.Key, key))
                    return new RemovePreparation(branch.Source, Removed: false);

                var data = RemoveData(branch.Data, slot);
                var state = StateOf(branch, checked(branch.Count - 1)) with
                {
                    DataMap = branch.DataMap ^ bit,
                    Data = data,
                    DataOwned = true,
                };
                return new RemovePreparation(RebuildBranch(branch, state), Removed: true);
            }

            if ((branch.NodeMap & bit) == 0)
                return new RemovePreparation(branch.Source, Removed: false);

            var childSlot = Slot(branch.NodeMap, bit);
            var oldChild = branch.Children[childSlot];
            var prepared = PrepareRemove(oldChild, key, hash, shift + BitsPerLevel);
            if (!prepared.Removed)
                return new RemovePreparation(branch.Source, Removed: false);

            var count = checked(branch.Count - 1);
            if (prepared.Node is null)
            {
                var children = RemoveChild(branch.Children, childSlot);
                var state = StateOf(branch, count) with
                {
                    NodeMap = branch.NodeMap ^ bit,
                    Children = children,
                    ChildrenOwned = true,
                };
                return new RemovePreparation(RebuildBranch(branch, state), Removed: true);
            }

            if (prepared.Node is LeafNode leaf)
            {
                var data = InsertData(branch.Data, Slot(branch.DataMap, bit), Entry.From(leaf));
                var children = RemoveChild(branch.Children, childSlot);
                var state = new BranchState(
                    branch.DataMap | bit,
                    data,
                    true,
                    branch.NodeMap ^ bit,
                    children,
                    true,
                    count);
                return new RemovePreparation(RebuildBranch(branch, state), Removed: true);
            }

            if (ReferenceEquals(prepared.Node, oldChild))
            {
                if (!IsOwned(branch))
                    throw new InvalidOperationException("An active child cannot occur beneath an unowned branch.");
                var state = StateOf(branch, count);
                return new RemovePreparation(FinishBranch(branch, state), Removed: true);
            }

            // Replacing the sole branch child with a hash node contracts this parent outright.
            // Do not enqueue an in-place parent write: publication must expose the promoted hash
            // node, while any already-prepared descendant commits remain valid within that node.
            if (branch.Data.Length == 0
                && branch.Children.Length == 1
                && prepared.Node is HashNode)
            {
                return new RemovePreparation(prepared.Node, Removed: true);
            }

            if (CanWriteChildren(branch))
            {
                var state = StateOf(branch, count);
                return new RemovePreparation(
                    FinishBranch(
                        branch,
                        state,
                        writeChild: true,
                        childIndex: childSlot,
                        child: prepared.Node),
                    Removed: true);
            }

            var replacedChildren = CloneChildren(branch.Children);
            replacedChildren[childSlot] = prepared.Node;
            var replacedState = StateOf(branch, count) with
            {
                Children = replacedChildren,
                ChildrenOwned = true,
            };
            return new RemovePreparation(RebuildBranch(branch, replacedState), Removed: true);
        }

        private LeafNode AllocateLeaf(uint hash, TKey key, TValue value)
        {
            Hit(OwnerTokenKernelFailurePoint.BeforeNodeAllocation);
            var result = new LeafNode(hash, key, value);
            if (_diagnostics is { } diagnostics)
                diagnostics.Counters.AllocatedNodeCount++;
            return result;
        }

        private HashNode AllocateCollision(
            uint hash,
            Entry[] entries,
            bool copiedNode)
        {
            Hit(OwnerTokenKernelFailurePoint.BeforeNodeAllocation);
            HashNode result = new SeparateTransientCollisionNode(
                hash,
                entries,
                GetOrCreateToken(),
                entriesOwned: true);
            if (_diagnostics is { } diagnostics)
            {
                diagnostics.Counters.AllocatedNodeCount++;
                if (copiedNode)
                    diagnostics.Counters.CopiedNodeCount++;
            }
            return result;
        }

        private Node AllocateBranch(
            BranchState state,
            bool copiedNode)
        {
            Hit(OwnerTokenKernelFailurePoint.BeforeNodeAllocation);
            Node result = new SeparateTransientBranchNode(
                state.DataMap,
                state.Data,
                state.DataOwned,
                state.NodeMap,
                state.Children,
                state.ChildrenOwned,
                state.Count,
                GetOrCreateToken());
            if (_diagnostics is { } diagnostics)
            {
                diagnostics.Counters.AllocatedNodeCount++;
                if (copiedNode)
                    diagnostics.Counters.CopiedNodeCount++;
            }
            return result;
        }

        private Entry[] AllocateDataArray(int length, bool copiesExisting)
        {
            if (length == 0)
                return [];
            Hit(OwnerTokenKernelFailurePoint.BeforeDataArrayAllocation);
            var result = new Entry[length];
            if (_diagnostics is { } diagnostics)
            {
                diagnostics.Counters.AllocatedArrayCount++;
                if (copiesExisting)
                    diagnostics.Counters.CopiedArrayCount++;
            }
            return result;
        }

        private Entry[] AllocateCollisionArray(int length, bool copiesExisting)
        {
            Hit(OwnerTokenKernelFailurePoint.BeforeCollisionArrayAllocation);
            var result = new Entry[length];
            if (_diagnostics is { } diagnostics)
            {
                diagnostics.Counters.AllocatedArrayCount++;
                if (copiesExisting)
                    diagnostics.Counters.CopiedArrayCount++;
            }
            return result;
        }

        private Node[] AllocateChildArray(int length, bool copiesExisting)
        {
            if (length == 0)
                return [];
            Hit(OwnerTokenKernelFailurePoint.BeforeChildArrayAllocation);
            var result = new Node[length];
            if (_diagnostics is { } diagnostics)
            {
                diagnostics.Counters.AllocatedArrayCount++;
                if (copiesExisting)
                    diagnostics.Counters.CopiedArrayCount++;
            }
            return result;
        }

        private Entry[] CloneData(Entry[] source)
        {
            var result = AllocateDataArray(source.Length, copiesExisting: true);
            Array.Copy(source, result, source.Length);
            return result;
        }

        private Node[] CloneChildren(Node[] source)
        {
            var result = AllocateChildArray(source.Length, copiesExisting: true);
            Array.Copy(source, result, source.Length);
            return result;
        }

        private Entry[] InsertData(Entry[] source, int index, Entry value)
        {
            var result = AllocateDataArray(source.Length + 1, copiesExisting: source.Length != 0);
            if (index > 0)
                Array.Copy(source, 0, result, 0, index);
            result[index] = value;
            if (index < source.Length)
                Array.Copy(source, index, result, index + 1, source.Length - index);
            return result;
        }

        private Entry[] RemoveData(Entry[] source, int index)
        {
            var result = AllocateDataArray(source.Length - 1, copiesExisting: source.Length > 1);
            if (index > 0)
                Array.Copy(source, 0, result, 0, index);
            if (index < source.Length - 1)
                Array.Copy(source, index + 1, result, index, source.Length - index - 1);
            return result;
        }

        private Node[] InsertChild(Node[] source, int index, Node value)
        {
            var result = AllocateChildArray(source.Length + 1, copiesExisting: source.Length != 0);
            if (index > 0)
                Array.Copy(source, 0, result, 0, index);
            result[index] = value;
            if (index < source.Length)
                Array.Copy(source, index, result, index + 1, source.Length - index);
            return result;
        }

        private Node[] RemoveChild(Node[] source, int index)
        {
            var result = AllocateChildArray(source.Length - 1, copiesExisting: source.Length > 1);
            if (index > 0)
                Array.Copy(source, 0, result, 0, index);
            if (index < source.Length - 1)
                Array.Copy(source, index + 1, result, index, source.Length - index - 1);
            return result;
        }

        private HashNode FinishCollision(
            HashNode source,
            Entry[] entries,
            bool writeEntry = false,
            int entryIndex = 0,
            Entry entry = default)
        {
            if (source is SeparateTransientCollisionNode owned && IsOwned(owned))
            {
                AddCommit(CommitStep.ForCollision(owned, entries, writeEntry, entryIndex, entry));
                return owned;
            }

            return AllocateCollision(source.Hash, entries, copiedNode: true);
        }

        private Node FinishBranch(
            BranchView source,
            BranchState state,
            bool writeData = false,
            int dataIndex = 0,
            Entry dataEntry = default,
            bool writeChild = false,
            int childIndex = 0,
            Node? child = null)
        {
            if (source.Source is SeparateTransientBranchNode owned && IsOwned(source))
            {
                AddCommit(CommitStep.ForBranch(
                    owned,
                    state,
                    writeData,
                    dataIndex,
                    dataEntry,
                    writeChild,
                    childIndex,
                    child));
                return owned;
            }

            return AllocateBranch(state, copiedNode: true);
        }

        private Node? RebuildBranch(BranchView source, BranchState state)
        {
            if (state.Data.Length == 0 && state.Children.Length == 0)
                return null;
            if (state.Data.Length == 1 && state.Children.Length == 0)
            {
                var entry = state.Data[0];
                return AllocateLeaf(entry.Hash, entry.Key, entry.Value);
            }
            if (state.Data.Length == 0 && state.Children.Length == 1 && state.Children[0] is HashNode)
                return state.Children[0];
            return FinishBranch(source, state);
        }

        private Node MergeHashNodesTransient(HashNode left, LeafNode right, int shift)
        {
            if (left.Hash == right.Hash)
            {
                var leftLeaf = (LeafNode)left;
                var entries = AllocateCollisionArray(2, copiesExisting: false);
                entries[0] = Entry.From(leftLeaf);
                entries[1] = Entry.From(right);
                return AllocateCollision(left.Hash, entries, copiedNode: false);
            }

            if (shift >= 32)
                throw new InvalidOperationException("Different 32-bit hashes cannot share every HAMT level.");

            var leftIndex = Index(left.Hash, shift);
            var rightIndex = Index(right.Hash, shift);
            var leftBit = Bit(leftIndex);
            var rightBit = Bit(rightIndex);
            if (leftIndex == rightIndex)
            {
                var child = MergeHashNodesTransient(left, right, shift + BitsPerLevel);
                var children = AllocateChildArray(1, copiesExisting: false);
                children[0] = child;
                return AllocateBranch(
                    new BranchState(0, [], false, leftBit, children, true, child.Count),
                    copiedNode: false);
            }

            if (left is LeafNode leftLeafNode)
            {
                var data = AllocateDataArray(2, copiesExisting: false);
                if (leftIndex < rightIndex)
                {
                    data[0] = Entry.From(leftLeafNode);
                    data[1] = Entry.From(right);
                }
                else
                {
                    data[0] = Entry.From(right);
                    data[1] = Entry.From(leftLeafNode);
                }
                return AllocateBranch(
                    new BranchState(leftBit | rightBit, data, true, 0, [], false, 2),
                    copiedNode: false);
            }

            var singletonData = AllocateDataArray(1, copiesExisting: false);
            singletonData[0] = Entry.From(right);
            var singletonChild = AllocateChildArray(1, copiesExisting: false);
            singletonChild[0] = left;
            return AllocateBranch(
                new BranchState(rightBit, singletonData, true, leftBit, singletonChild, true, checked(left.Count + 1)),
                copiedNode: false);
        }

        private bool IsOwned(HashNode collision) =>
            collision is SeparateTransientCollisionNode separate
            && _token is { } token
            && ReferenceEquals(separate.Owner, token);

        private bool CanWriteEntries(HashNode collision) =>
            collision is SeparateTransientCollisionNode separate
            && _token is { } token
            && separate.CanWriteEntries(token);

        private bool IsOwned(BranchView branch) =>
            branch.Source is SeparateTransientBranchNode separate
            && _token is { } token
            && ReferenceEquals(separate.Owner, token);

        private bool CanWriteData(BranchView branch) =>
            branch.Source is SeparateTransientBranchNode separate
            && _token is { } token
            && separate.CanWriteData(token);

        private bool CanWriteChildren(BranchView branch) =>
            branch.Source is SeparateTransientBranchNode separate
            && _token is { } token
            && separate.CanWriteChildren(token);

        private BranchState StateOf(BranchView source, int count) =>
            new(
                source.DataMap,
                source.Data,
                source.Source is SeparateTransientBranchNode separateData
                    && _token is { } dataToken
                    && ReferenceEquals(separateData.Owner, dataToken)
                    && separateData.DataOwned,
                source.NodeMap,
                source.Children,
                source.Source is SeparateTransientBranchNode separateChildren
                    && _token is { } childToken
                    && ReferenceEquals(separateChildren.Owner, childToken)
                    && separateChildren.ChildrenOwned,
                count);

        private void AddCommit(CommitStep step)
        {
            if (_commitCount == MaximumCommitSteps)
                throw new InvalidOperationException("A CHAMP edit exceeded the bounded commit-plan depth.");
            if (_commits is null)
            {
                Hit(OwnerTokenKernelFailurePoint.BeforeCommitPlanAllocation);
                _commits = new CommitStep[MaximumCommitSteps];
                if (_diagnostics is { } allocationDiagnostics)
                    allocationDiagnostics.Counters.CommitPlanAllocationCount++;
            }

            _commits[_commitCount++] = step;
            if (_diagnostics is { } diagnostics)
            {
                diagnostics.Counters.InPlaceNodeMutationCount++;
                if (step.WritesArray)
                    diagnostics.Counters.InPlaceArrayWriteCount++;
            }
        }

        private void ApplyCommits()
        {
            for (var index = 0; index < _commitCount; index++)
                _commits![index].Apply();
        }

        private void ClearCommitPlan()
        {
            for (var index = 0; index < _commitCount; index++)
                _commits![index] = default;
            _commitCount = 0;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private void EnsureActive()
        {
            if (Volatile.Read(ref _state) != ActiveState)
                ThrowInactive();
        }

        [DoesNotReturn]
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void ThrowInactive() =>
            throw new ObjectDisposedException(nameof(OwnerTokenTransientKernel));

        private EditToken GetOrCreateToken()
        {
            if (_token is not null)
                return _token;

            Hit(OwnerTokenKernelFailurePoint.BeforeTokenAllocation);
            _token = new EditToken();
            return _token;
        }

        private MutableCounters CaptureCounters() => _diagnostics?.Counters ?? default;

        private void RestoreCounters(MutableCounters counters)
        {
            if (_diagnostics is not null)
                _diagnostics.Counters = counters;
        }

        private void RestoreTransientResources(EditToken? token, CommitStep[]? commitPlan)
        {
            if (_commits is not null)
            {
                for (var index = 0; index < _commitCount; index++)
                    _commits[index] = default;
            }

            _commitCount = 0;
            _token = token;
            _commits = commitPlan;
        }

        private void Hit(OwnerTokenKernelFailurePoint point) =>
            _diagnostics?.FailureInjector?.Invoke(point);

        private readonly record struct SetPreparation(Node Node, bool Changed, bool Added, int CountDelta);

        private readonly record struct RemovePreparation(Node? Node, bool Removed);

        private readonly record struct BranchState(
            uint DataMap,
            Entry[] Data,
            bool DataOwned,
            uint NodeMap,
            Node[] Children,
            bool ChildrenOwned,
            int Count);

        private readonly struct CommitStep
        {
            private readonly SeparateTransientBranchNode? _branch;
            private readonly SeparateTransientCollisionNode? _collision;
            private readonly BranchState _branchState;
            private readonly Entry[]? _collisionEntries;
            private readonly Entry _entry;
            private readonly Node? _child;
            private readonly int _entryIndex;
            private readonly int _childIndex;
            private readonly byte _flags;

            private CommitStep(
                SeparateTransientBranchNode? branch,
                SeparateTransientCollisionNode? collision,
                BranchState branchState,
                Entry[]? collisionEntries,
                Entry entry,
                Node? child,
                int entryIndex,
                int childIndex,
                byte flags)
            {
                _branch = branch;
                _collision = collision;
                _branchState = branchState;
                _collisionEntries = collisionEntries;
                _entry = entry;
                _child = child;
                _entryIndex = entryIndex;
                _childIndex = childIndex;
                _flags = flags;
            }

            internal bool WritesArray => (_flags & 3) != 0;

            internal static CommitStep ForCollision(
                SeparateTransientCollisionNode collision,
                Entry[] entries,
                bool writeEntry,
                int entryIndex,
                Entry entry) =>
                new(
                    branch: null,
                    collision,
                    branchState: default,
                    collisionEntries: entries,
                    entry,
                    child: null,
                    entryIndex,
                    childIndex: 0,
                    flags: (byte)(writeEntry ? 1 : 0));

            internal static CommitStep ForBranch(
                SeparateTransientBranchNode branch,
                BranchState state,
                bool writeData,
                int dataIndex,
                Entry dataEntry,
                bool writeChild,
                int childIndex,
                Node? child) =>
                new(
                    branch,
                    collision: null,
                    state,
                    collisionEntries: null,
                    dataEntry,
                    child,
                    dataIndex,
                    childIndex,
                    flags: (byte)((writeData ? 1 : 0) | (writeChild ? 2 : 0)));

            internal void Apply()
            {
                if (_collision is not null)
                {
                    _collision.CommitTransient(
                        _collisionEntries!,
                        entriesOwned: true,
                        writeEntry: (_flags & 1) != 0,
                        entryIndex: _entryIndex,
                        entry: _entry);
                    return;
                }

                _branch!.CommitTransient(
                    _branchState.DataMap,
                    _branchState.Data,
                    _branchState.DataOwned,
                    _branchState.NodeMap,
                    _branchState.Children,
                    _branchState.ChildrenOwned,
                    _branchState.Count,
                    writeData: (_flags & 1) != 0,
                    dataIndex: _entryIndex,
                    dataEntry: _entry,
                    writeChild: (_flags & 2) != 0,
                    childIndex: _childIndex,
                    child: _child);
            }
        }

        private sealed class KernelDiagnostics
        {
            internal Action<OwnerTokenKernelFailurePoint>? FailureInjector;
            internal MutableCounters Counters;
        }

        private struct MutableCounters
        {
            internal long AdoptionCount;
            internal long AdoptionNodeVisits;
            internal long PublicationCount;
            internal long PublicationNodeVisits;
            internal long DeferredPersistentMutationCount;
            internal long EditablePromotionCount;
            internal long CommitPlanAllocationCount;
            internal long PreparedMutationCount;
            internal long CopiedNodeCount;
            internal long AllocatedNodeCount;
            internal long CopiedArrayCount;
            internal long AllocatedArrayCount;
            internal long InPlaceNodeMutationCount;
            internal long InPlaceArrayWriteCount;
            internal long PersistentWrapperAllocationCount;
            internal long DeferredPersistentWrapperAllocationCount;

            internal readonly OwnerTokenKernelCounters Snapshot() =>
                new(
                    AdoptionCount,
                    AdoptionNodeVisits,
                    PublicationCount,
                    PublicationNodeVisits,
                    DeferredPersistentMutationCount,
                    EditablePromotionCount,
                    CommitPlanAllocationCount,
                    PreparedMutationCount,
                    CopiedNodeCount,
                    AllocatedNodeCount,
                    CopiedArrayCount,
                    AllocatedArrayCount,
                    InPlaceNodeMutationCount,
                    InPlaceArrayWriteCount,
                    PersistentWrapperAllocationCount,
                    DeferredPersistentWrapperAllocationCount);
        }
    }

    /// <summary>Recursively validates cached counts, routing, ownership sealing, and CHAMP contraction.</summary>
    internal PersistentHashMapCanonicalityDiagnostics ValidateCanonicalityForDiagnostics()
    {
        var nodeCount = 0;
        var recursiveCount = ValidateNode(
            _root,
            shift: 0,
            prefixMask: 0,
            prefix: 0,
            ref nodeCount);
        if (recursiveCount != _count)
        {
            throw new InvalidOperationException(
                $"Map count {_count} differs from recursive node count {recursiveCount}.");
        }

        return new PersistentHashMapCanonicalityDiagnostics(_count, recursiveCount, nodeCount);
    }

    private int ValidateNode(
        Node? node,
        int shift,
        uint prefixMask,
        uint prefix,
        ref int nodeCount)
    {
        if (node is null)
            return 0;

        nodeCount++;
        switch (node)
        {
            case LeafNode leaf:
                ValidateEntryHashAndPrefix(new Entry(leaf.Hash, leaf.Key, leaf.Value), prefixMask, prefix);
                return 1;

            case CollisionNode collision:
                return ValidateCollision(collision.Hash, collision.Entries, prefixMask, prefix);

            case SeparateTransientCollisionNode collision:
                if (collision.Owner.IsActive)
                    throw new InvalidOperationException("A published collision node retains an active edit token.");
                return ValidateCollision(collision.Hash, collision.Entries, prefixMask, prefix);

            case BitmapIndexedNode branch:
                return ValidateBranch(
                    BranchView.Create(branch),
                    shift,
                    prefix,
                    ref nodeCount);

            case SeparateTransientBranchNode branch:
                if (branch.Owner.IsActive)
                    throw new InvalidOperationException("A published branch retains an active edit token.");
                return ValidateBranch(
                    BranchView.Create(branch),
                    shift,
                    prefix,
                    ref nodeCount);

            default:
                throw new InvalidOperationException("Unknown CHAMP node kind.");
        }
    }

    private int ValidateCollision(
        uint hash,
        Entry[] entries,
        uint prefixMask,
        uint prefix)
    {
        if (entries.Length < 2)
            throw new InvalidOperationException("A collision node must contain at least two entries.");
        for (var left = 0; left < entries.Length; left++)
        {
            var entry = entries[left];
            if (entry.Hash != hash)
                throw new InvalidOperationException("A collision entry has a different full hash.");
            ValidateEntryHashAndPrefix(entry, prefixMask, prefix);
            for (var right = 0; right < left; right++)
            {
                if (_comparer.Equals(entries[right].Key, entry.Key))
                    throw new InvalidOperationException("A collision node contains equivalent keys.");
            }
        }
        return entries.Length;
    }

    private int ValidateBranch(
        BranchView branch,
        int shift,
        uint prefix,
        ref int nodeCount)
    {
        if ((branch.DataMap & branch.NodeMap) != 0)
            throw new InvalidOperationException("Branch data and node bitmaps overlap.");
        if (System.Numerics.BitOperations.PopCount(branch.DataMap) != branch.Data.Length)
            throw new InvalidOperationException("Branch data bitmap and array length differ.");
        if (System.Numerics.BitOperations.PopCount(branch.NodeMap) != branch.Children.Length)
            throw new InvalidOperationException("Branch node bitmap and array length differ.");
        if (branch.Data.Length == 0 && branch.Children.Length == 0)
            throw new InvalidOperationException("An empty branch must be contracted.");
        if (branch.Data.Length == 1 && branch.Children.Length == 0)
            throw new InvalidOperationException("A singleton data branch must be a leaf.");
        if (branch.Data.Length == 0 && branch.Children.Length == 1 && branch.Children[0] is HashNode)
            throw new InvalidOperationException("A singleton hash child must be promoted.");

        var recursiveCount = branch.Data.Length;
        var dataIndex = 0;
        var childIndex = 0;
        var nextShift = shift + BitsPerLevel;
        var nextMask = nextShift >= 32 ? uint.MaxValue : ((1u << nextShift) - 1);
        for (var slot = 0; slot < 32; slot++)
        {
            var bit = Bit(slot);
            var nextPrefix = prefix | ((uint)slot << shift);
            if ((branch.DataMap & bit) != 0)
                ValidateEntryHashAndPrefix(branch.Data[dataIndex++], nextMask, nextPrefix);
            if ((branch.NodeMap & bit) == 0)
                continue;
            var child = branch.Children[childIndex++];
            if (child is LeafNode)
                throw new InvalidOperationException("A leaf child must be inlined into its parent.");
            recursiveCount = checked(
                recursiveCount + ValidateNode(child, nextShift, nextMask, nextPrefix, ref nodeCount));
        }

        if (recursiveCount != branch.Count)
        {
            throw new InvalidOperationException(
                $"Branch cached count {branch.Count} differs from recursive count {recursiveCount}.");
        }
        return recursiveCount;
    }

    private void ValidateEntryHashAndPrefix(Entry entry, uint prefixMask, uint prefix)
    {
        var actualHash = unchecked((uint)_comparer.GetHashCode(entry.Key!));
        if (actualHash != entry.Hash)
            throw new InvalidOperationException("An entry's cached hash differs from its comparer hash.");
        if ((entry.Hash & prefixMask) != prefix)
            throw new InvalidOperationException("An entry is routed beneath the wrong bitmap slot.");
    }
}

internal readonly record struct PersistentHashMapCanonicalityDiagnostics(
    int EntryCount,
    int RecursiveEntryCount,
    int NodeCount);
