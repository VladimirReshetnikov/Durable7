using System.Buffers.Binary;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.Hamt;

public sealed partial class MerkleSearchTree<TKey, TValue>
{
    private static readonly byte[] ProofMagic = "MSP2"u8.ToArray();

    /// <summary>Writes this tree's complete block closure to a content-addressed store.</summary>
    /// <param name="store">The destination store.</param>
    /// <returns>The number of blocks newly added to the store.</returns>
    /// <remarks>All address conflicts are checked before the first write.</remarks>
    public int Save(IMerkleBlockStore store)
    {
        ArgumentNullException.ThrowIfNull(store);
        var pack = ExportPack();
        PreflightStore(pack.Blocks, store);
        var added = 0;
        foreach (var block in pack.Blocks)
            added += store.Put(block) ? 1 : 0;
        return added;
    }

    /// <summary>Exports this tree's complete block closure in deterministic preorder.</summary>
    public MerkleBlockPack ExportPack() => new(
        MerkleSearchTreePolicy<TKey, TValue>.AlgorithmId,
        Policy.DomainDigest,
        RootHash,
        EnumerateBlocks(_root).Select(static node => new MerkleBlock(node.Digest, node.BlockBytes)));

    /// <summary>Exports explicitly requested blocks from this tree.</summary>
    /// <param name="digests">Unique block digests in desired transfer order.</param>
    /// <exception cref="ArgumentException">A digest is repeated or does not name a block in this tree.</exception>
    public MerkleBlockPack ExportPack(IEnumerable<MerkleDigest> digests)
    {
        ArgumentNullException.ThrowIfNull(digests);
        var byDigest = EnumerateBlocks(_root).ToDictionary(static node => node.Digest);
        var requested = new List<MerkleBlock>();
        var unique = new HashSet<MerkleDigest>();
        foreach (var digest in digests)
        {
            if (!unique.Add(digest))
                throw new ArgumentException($"Digest '{digest}' was requested more than once.", nameof(digests));
            if (!byDigest.TryGetValue(digest, out var node))
                throw new ArgumentException($"Digest '{digest}' does not name a block in this tree.", nameof(digests));
            requested.Add(new MerkleBlock(node.Digest, node.BlockBytes));
        }
        return new MerkleBlockPack(
            MerkleSearchTreePolicy<TKey, TValue>.AlgorithmId,
            Policy.DomainDigest,
            RootHash,
            requested);
    }

    /// <summary>Exports every target block absent from a receiver, pruning known subtrees.</summary>
    /// <param name="receiverStore">The receiver's verified closure store.</param>
    /// <remarks>
    /// Presence of a block is treated as presence of its verified descendant closure. Use
    /// <see cref="PlanSync"/> for iterative repair of a partial store.
    /// </remarks>
    public MerkleBlockPack CreateSyncPack(IMerkleBlockStore receiverStore)
    {
        ArgumentNullException.ThrowIfNull(receiverStore);
        var missing = new List<MerkleBlock>();
        CollectMissingClosure(_root, receiverStore, missing);
        return new MerkleBlockPack(
            MerkleSearchTreePolicy<TKey, TValue>.AlgorithmId,
            Policy.DomainDigest,
            RootHash,
            missing);
    }

    /// <summary>Plans one round of frontier-based block synchronization toward this tree.</summary>
    /// <param name="localTree">The receiver's currently published tree.</param>
    /// <param name="receiverStore">The receiver's possibly partial block store.</param>
    /// <returns>A plan requesting the first absent block on every reachable target path.</returns>
    public MerkleSyncPlan PlanSync(
        MerkleSearchTree<TKey, TValue> localTree,
        IMerkleBlockStore receiverStore)
    {
        EnsureCompatible(localTree);
        ArgumentNullException.ThrowIfNull(receiverStore);
        if (RootHash == localTree.RootHash)
        {
            return new MerkleSyncPlan(
                MerkleSearchTreePolicy<TKey, TValue>.AlgorithmId,
                Policy.DomainDigest,
                localTree.RootHash,
                RootHash,
                [],
                0,
                0);
        }

        var requested = new List<MerkleDigest>();
        var examinedBlocks = 0;
        long examinedBytes = 0;
        CollectMissingFrontier(_root, receiverStore, requested, ref examinedBlocks, ref examinedBytes);
        return new MerkleSyncPlan(
            MerkleSearchTreePolicy<TKey, TValue>.AlgorithmId,
            Policy.DomainDigest,
            localTree.RootHash,
            RootHash,
            requested,
            examinedBlocks,
            examinedBytes);
    }

    /// <summary>Loads and fully verifies a tree closure from a block store.</summary>
    /// <param name="rootHash">The expected root hash.</param>
    /// <param name="policy">The expected comparison, codec, and hash domain.</param>
    /// <param name="store">The source block store.</param>
    /// <param name="budget">Finite verification limits, or the bounded defaults.</param>
    /// <exception cref="MerkleVerificationException">The closure is missing, malformed, noncanonical, or structurally invalid.</exception>
    public static MerkleSearchTree<TKey, TValue> Load(
        MerkleDigest rootHash,
        MerkleSearchTreePolicy<TKey, TValue> policy,
        IMerkleBlockStore store,
        MerkleVerificationBudget? budget = null)
    {
        ArgumentNullException.ThrowIfNull(policy);
        ArgumentNullException.ThrowIfNull(store);
        var context = new VerificationContext(budget ?? MerkleVerificationBudget.Default);
        return LoadWithContext(rootHash, policy, store, context);
    }

    private static MerkleSearchTree<TKey, TValue> LoadWithContext(
        MerkleDigest rootHash,
        MerkleSearchTreePolicy<TKey, TValue> policy,
        IMerkleBlockStore store,
        VerificationContext context)
    {
        var tree = Create(policy);
        if (rootHash == policy.EmptyDigest)
            return tree;
        var cache = new Dictionary<MerkleDigest, Node>();
        var active = new HashSet<MerkleDigest>();
        var root = tree.LoadNode(rootHash, store, context, cache, active, 1);
        if (root.Digest != rootHash)
            throw Verification(MerkleVerificationFailureKind.RootMismatch, "The reconstructed root does not match the requested root hash.", rootHash);
        tree = new MerkleSearchTree<TKey, TValue>(root, policy);
        _ = tree.ValidateStructure();
        return tree;
    }

    /// <summary>Verifies a pack against its declared root and optionally commits it to a store.</summary>
    /// <param name="pack">A complete pack, or a partial pack completed by <paramref name="destinationStore"/>.</param>
    /// <param name="policy">The expected comparison, codec, and hash domain.</param>
    /// <param name="destinationStore">An optional existing closure store and commit destination.</param>
    /// <param name="budget">Finite verification limits, or the bounded defaults.</param>
    public static MerkleSearchTree<TKey, TValue> Import(
        MerkleBlockPack pack,
        MerkleSearchTreePolicy<TKey, TValue> policy,
        IMerkleBlockStore? destinationStore = null,
        MerkleVerificationBudget? budget = null)
    {
        ArgumentNullException.ThrowIfNull(pack);
        ArgumentNullException.ThrowIfNull(policy);
        VerifyEnvelope(pack.AlgorithmId, pack.DomainDigest, policy);

        var limits = budget ?? MerkleVerificationBudget.Default;
        var verifier = Create(policy);
        var context = new VerificationContext(limits);
        foreach (var block in pack.Blocks)
            _ = verifier.DecodeBlock(block, context, 1);

        var staged = pack.Blocks.ToDictionary(static block => block.Digest);
        var overlay = new OverlayBlockStore(staged, destinationStore);
        var loaded = LoadWithContext(pack.RootHash, policy, overlay, context);
        if (destinationStore is not null)
        {
            PreflightStore(pack.Blocks, destinationStore);
            foreach (var block in pack.Blocks)
                _ = destinationStore.Put(block);
        }
        return loaded;
    }

    /// <summary>Creates a canonical membership or non-membership proof for one key.</summary>
    public MerkleProof CreateProof(TKey key)
    {
        var keyBytes = EncodeOwned(Policy.KeyCodec, key);
        var steps = new List<MerkleProofStep>();
        var node = _root;
        var kind = MerkleProofKind.NonMembership;
        byte[]? valueBytes = null;
        while (node is not null)
        {
            var position = FindPosition(node.Entries, key, out var found);
            var expanded = !found && node.Children[position] is not null ? new[] { position } : [];
            steps.Add(new MerkleProofStep(new MerkleBlock(node.Digest, node.BlockBytes), expanded));
            if (found)
            {
                kind = MerkleProofKind.Membership;
                valueBytes = node.Entries[position].ValueBytes;
                break;
            }
            node = node.Children[position];
        }
        return new MerkleProof(
            MerkleSearchTreePolicy<TKey, TValue>.AlgorithmId,
            Policy.DomainDigest,
            RootHash,
            kind,
            EncodePointQuery(kind, keyBytes, valueBytes),
            steps);
    }

    /// <summary>Creates a canonical completeness proof for an inclusive key range.</summary>
    public MerkleProof CreateRangeProof(TKey minimumKey, TKey maximumKey)
    {
        if (Policy.Comparer.Compare(minimumKey, maximumKey) > 0)
            throw new ArgumentException("The minimum key must not follow the maximum key.", nameof(minimumKey));
        var steps = new List<MerkleProofStep>();
        CollectRangeProof(_root, minimumKey, maximumKey, steps);
        return new MerkleProof(
            MerkleSearchTreePolicy<TKey, TValue>.AlgorithmId,
            Policy.DomainDigest,
            RootHash,
            MerkleProofKind.Range,
            EncodeRangeQuery(
                EncodeOwned(Policy.KeyCodec, minimumKey),
                EncodeOwned(Policy.KeyCodec, maximumKey)),
            steps);
    }

    /// <summary>Verifies a membership, non-membership, or range proof without trusting its bytes.</summary>
    public static MerkleProofVerificationResult VerifyProof(
        MerkleProof proof,
        MerkleSearchTreePolicy<TKey, TValue> policy,
        MerkleVerificationBudget? budget = null)
    {
        ArgumentNullException.ThrowIfNull(proof);
        ArgumentNullException.ThrowIfNull(policy);
        var context = new VerificationContext(budget ?? MerkleVerificationBudget.Default);
        try
        {
            // Query accounting is deliberately first. Proof-shape limits then run before envelope
            // checks, verifier-side collections, codec callbacks, hashing, or block decoding.
            context.AccountProofQuery(proof.Query.Length, proof.RootHash);
            context.PreflightProofStructure(proof);
            VerifyEnvelope(proof.AlgorithmId, proof.DomainDigest, policy);
            var verifier = Create(policy);
            var decoded = new Dictionary<MerkleDigest, DecodedBlock>();
            var steps = new Dictionary<MerkleDigest, MerkleProofStep>();
            foreach (var step in proof.Steps)
            {
                var block = verifier.DecodeBlock(step.Block, context, 1);
                decoded.Add(step.Block.Digest, block);
                steps.Add(step.Block.Digest, step);
                foreach (var index in step.ExpandedChildIndexes)
                {
                    if ((uint)index >= (uint)block.ChildDigests.Length)
                        throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A proof expands a child index outside its block.", step.Block.Digest);
                    if (block.ChildDigests[index] == policy.EmptyDigest)
                        throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A proof expands an empty child interval.", step.Block.Digest);
                }
            }

            if (proof.RootHash == policy.EmptyDigest)
            {
                if (proof.Steps.Count != 0)
                    throw Verification(MerkleVerificationFailureKind.ProofMismatch, "An empty-root proof must not contain blocks.", proof.RootHash);
                VerifyEmptyProofQuery(proof, policy);
                return MerkleProofVerificationResult.Success(
                    proof.RootHash,
                    context.BlockCount,
                    context.TotalBytes);
            }
            if (!decoded.ContainsKey(proof.RootHash))
                throw Verification(MerkleVerificationFailureKind.RootMismatch, "The proof does not contain its declared root block.", proof.RootHash);

            var visited = new HashSet<MerkleDigest>();
            switch (proof.Kind)
            {
                case MerkleProofKind.Membership:
                case MerkleProofKind.NonMembership:
                    verifier.VerifyPointProof(proof, decoded, steps, visited, context);
                    break;
                case MerkleProofKind.Range:
                    verifier.VerifyRangeProof(proof, decoded, steps, visited, context);
                    break;
                default:
                    throw Verification(MerkleVerificationFailureKind.ProofMismatch, "The proof kind is not supported.", proof.RootHash);
            }
            if (visited.Count != proof.Steps.Count)
                throw Verification(MerkleVerificationFailureKind.ProofMismatch, "The proof contains blocks outside the canonical query expansion.", proof.RootHash);
            return MerkleProofVerificationResult.Success(proof.RootHash, context.BlockCount, context.TotalBytes);
        }
        catch (MerkleVerificationException exception)
        {
            return MerkleProofVerificationResult.Failure(
                exception.FailureKind,
                exception.Message,
                context.BlockCount,
                context.TotalBytes,
                proof.RootHash);
        }
        catch (Exception exception) when (exception is FormatException or ArgumentException or OverflowException)
        {
            return MerkleProofVerificationResult.Failure(
                MerkleVerificationFailureKind.MalformedBlock,
                exception.Message,
                context.BlockCount,
                context.TotalBytes,
                proof.RootHash);
        }
    }

    /// <summary>Combines two descendants relative to a common base with typed conflict resolution.</summary>
    public static MerkleThreeWayMergeResult<TKey, TValue> Merge(
        MerkleSearchTree<TKey, TValue> baseTree,
        MerkleSearchTree<TKey, TValue> left,
        MerkleSearchTree<TKey, TValue> right,
        MerkleThreeWayMergeResolver<TKey, TValue>? resolver = null,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        ArgumentNullException.ThrowIfNull(baseTree);
        baseTree.EnsureCompatible(left);
        baseTree.EnsureCompatible(right);
        if (left.RootHash == baseTree.RootHash)
            return MerkleThreeWayMergeResult<TKey, TValue>.Success(right);
        if (right.RootHash == baseTree.RootHash || left.RootHash == right.RootHash)
            return MerkleThreeWayMergeResult<TKey, TValue>.Success(left);

        var values = valueComparer ?? EqualityComparer<TValue>.Default;
        var baseEntries = baseTree.ToArray();
        var leftEntries = left.ToArray();
        var rightEntries = right.ToArray();
        var output = new List<KeyValuePair<TKey, TValue>>();
        var conflicts = new List<MerkleThreeWayMergeConflict<TKey, TValue>>();
        var bi = 0;
        var li = 0;
        var ri = 0;
        while (bi < baseEntries.Length || li < leftEntries.Length || ri < rightEntries.Length)
        {
            var key = MinimumKey(baseTree.Policy.Comparer, baseEntries, bi, leftEntries, li, rightEntries, ri);
            var baseSide = TakeSide(baseTree.Policy.Comparer, baseEntries, ref bi, key);
            var leftSide = TakeSide(baseTree.Policy.Comparer, leftEntries, ref li, key);
            var rightSide = TakeSide(baseTree.Policy.Comparer, rightEntries, ref ri, key);

            if (MergeValuesEqual(leftSide.Value, baseSide.Value, values))
                AppendMergeSide(output, rightSide, leftSide, baseSide);
            else if (MergeValuesEqual(rightSide.Value, baseSide.Value, values))
                AppendMergeSide(output, leftSide, rightSide, baseSide);
            else if (MergeValuesEqual(leftSide.Value, rightSide.Value, values))
                AppendMergeSide(output, leftSide, rightSide, baseSide);
            else
            {
                var representative = leftSide.HasKey ? leftSide.Key! : rightSide.HasKey ? rightSide.Key! : baseSide.Key!;
                var conflict = new MerkleThreeWayMergeConflict<TKey, TValue>(
                    representative,
                    baseSide.Value,
                    leftSide.Value,
                    rightSide.Value);
                var resolution = resolver?.Invoke(conflict) ?? MerkleMergeResolution<TValue>.Unresolved;
                if (!ApplyResolution(output, conflict.Key, resolution, baseSide, leftSide, rightSide))
                    conflicts.Add(conflict);
            }
        }

        return conflicts.Count != 0
            ? MerkleThreeWayMergeResult<TKey, TValue>.Conflicted(conflicts)
            : MerkleThreeWayMergeResult<TKey, TValue>.Success(CreateRange(output, baseTree.Policy));
    }

    private static IEnumerable<Node> EnumerateBlocks(Node? root)
    {
        if (root is null)
            yield break;
        var stack = new Stack<Node>();
        stack.Push(root);
        while (stack.TryPop(out var node))
        {
            yield return node;
            for (var index = node.Children.Length - 1; index >= 0; index--)
            {
                if (node.Children[index] is { } child)
                    stack.Push(child);
            }
        }
    }

    private static void PreflightStore(IEnumerable<MerkleBlock> blocks, IMerkleBlockStore store)
    {
        foreach (var block in blocks)
        {
            if (store.TryGet(block.Digest, out var existing) && !existing.Equals(block))
            {
                throw Verification(
                    MerkleVerificationFailureKind.ConflictingBlock,
                    $"Digest '{block.Digest}' is already associated with different block bytes.",
                    block.Digest);
            }
        }
    }

    private static void CollectMissingClosure(
        Node? node,
        IMerkleBlockStore receiver,
        List<MerkleBlock> missing)
    {
        if (node is null || receiver.Contains(node.Digest))
            return;
        missing.Add(new MerkleBlock(node.Digest, node.BlockBytes));
        foreach (var child in node.Children)
            CollectMissingClosure(child, receiver, missing);
    }

    private static void CollectMissingFrontier(
        Node? node,
        IMerkleBlockStore receiver,
        List<MerkleDigest> requested,
        ref int examinedBlocks,
        ref long examinedBytes)
    {
        if (node is null)
            return;
        examinedBlocks = checked(examinedBlocks + 1);
        examinedBytes = checked(examinedBytes + node.BlockBytes.Length);
        if (!receiver.Contains(node.Digest))
        {
            requested.Add(node.Digest);
            return;
        }
        foreach (var child in node.Children)
            CollectMissingFrontier(child, receiver, requested, ref examinedBlocks, ref examinedBytes);
    }

    private Node LoadNode(
        MerkleDigest digest,
        IMerkleBlockStore store,
        VerificationContext context,
        Dictionary<MerkleDigest, Node> cache,
        HashSet<MerkleDigest> active,
        int depth)
    {
        context.CheckDepth(depth, digest);
        if (cache.TryGetValue(digest, out var cached))
            return cached;
        if (!active.Add(digest))
            throw Verification(MerkleVerificationFailureKind.CycleDetected, "The Merkle closure contains a reference cycle.", digest);
        try
        {
            if (!store.TryGet(digest, out var block))
                throw Verification(MerkleVerificationFailureKind.MissingBlock, $"Required Merkle block '{digest}' is absent.", digest);
            var decoded = DecodeBlock(block, context, depth);
            var children = new Node?[decoded.ChildDigests.Length];
            for (var index = 0; index < children.Length; index++)
            {
                var childDigest = decoded.ChildDigests[index];
                if (childDigest == Policy.EmptyDigest)
                    continue;
                var child = LoadNode(childDigest, store, context, cache, active, checked(depth + 1));
                ValidateReference(decoded, index, child);
                children[index] = child;
            }

            var actualCount = decoded.Entries.Length;
            foreach (var child in children)
                actualCount = checked(actualCount + (child?.Count ?? 0));
            if (actualCount != decoded.SubtreeCount)
            {
                throw Verification(
                    MerkleVerificationFailureKind.InvalidReference,
                    $"Block '{digest}' declares {decoded.SubtreeCount} entries but its closure contains {actualCount}.",
                    digest);
            }

            var node = NewNode(decoded.Level, decoded.Entries, children);
            if (node.Digest != digest || !node.BlockBytes.AsSpan().SequenceEqual(block.Content))
                throw Verification(MerkleVerificationFailureKind.NonCanonicalBlock, "Decoded block bytes do not round-trip canonically.", digest);
            cache.Add(digest, node);
            return node;
        }
        finally
        {
            _ = active.Remove(digest);
        }
    }

    private void ValidateReference(DecodedBlock parent, int childIndex, Node child)
    {
        if (child.Level >= parent.Level)
            throw Verification(MerkleVerificationFailureKind.InvalidReference, "A child block is not below its parent layer.", child.Digest);
        if (childIndex != 0 && Policy.Comparer.Compare(child.MinimumKey, parent.Entries[childIndex - 1].Key) <= 0)
            throw Verification(MerkleVerificationFailureKind.InvalidReference, "A child block crosses its lower key separator.", child.Digest);
        if (childIndex != parent.Entries.Length && Policy.Comparer.Compare(child.MaximumKey, parent.Entries[childIndex].Key) >= 0)
            throw Verification(MerkleVerificationFailureKind.InvalidReference, "A child block crosses its upper key separator.", child.Digest);
    }

    private DecodedBlock DecodeBlock(MerkleBlock block, VerificationContext context, int depth)
    {
        var firstVisit = context.Account(block, depth);
        if (Policy.HashBytes(block.Content) != block.Digest)
            throw Verification(MerkleVerificationFailureKind.DigestMismatch, "A block's bytes do not match its claimed digest.", block.Digest);

        var bytes = block.Content;
        var offset = 0;
        if (bytes.Length < BlockHeaderLength + DigestLength)
            throw Verification(MerkleVerificationFailureKind.MalformedBlock, "A Merkle block is shorter than the minimum encoding.", block.Digest);
        if (!Take(bytes, ref offset, BlockMagic.Length).SequenceEqual(BlockMagic))
            throw Verification(MerkleVerificationFailureKind.MalformedBlock, "A Merkle block has the wrong magic.", block.Digest);
        if (bytes[offset++] != NodeBlockTag)
            throw Verification(MerkleVerificationFailureKind.MalformedBlock, "A Merkle block has an unsupported tag.", block.Digest);
        var domain = MerkleDigest.FromBytes(Take(bytes, ref offset, DigestLength));
        if (domain != Policy.DomainDigest)
            throw Verification(MerkleVerificationFailureKind.DomainMismatch, "A Merkle block belongs to another policy domain.", block.Digest);
        var level = bytes[offset++];
        if (level > 64)
            throw Verification(MerkleVerificationFailureKind.MalformedBlock, "A Merkle block layer exceeds the SHA-256 nibble range.", block.Digest);
        var subtreeCount = ReadInt32(bytes, ref offset);
        var entryCount = ReadInt32(bytes, ref offset);
        if (subtreeCount <= 0 || entryCount <= 0 || subtreeCount < entryCount)
            throw Verification(MerkleVerificationFailureKind.MalformedBlock, "A Merkle block has invalid entry counts.", block.Digest);
        if (entryCount >= context.Budget.MaxChildReferencesPerBlock)
            throw Verification(MerkleVerificationFailureKind.ResourceLimitExceeded, "A Merkle block exceeds the child-reference budget.", block.Digest);
        var minimumLength = checked((long)BlockHeaderLength + (long)entryCount * (sizeof(int) * 2 + DigestLength) + DigestLength);
        if (minimumLength > bytes.Length)
            throw Verification(MerkleVerificationFailureKind.MalformedBlock, "A Merkle block cannot contain its declared entries and child references.", block.Digest);
        if (firstVisit)
            context.AccountEntries(entryCount, block.Digest);

        var entries = new EntryRecord[entryCount];
        for (var index = 0; index < entries.Length; index++)
        {
            var keyBytes = ReadLengthPrefixed(bytes, ref offset, block.Digest);
            var valueBytes = ReadLengthPrefixed(bytes, ref offset, block.Digest);
            TKey key;
            TValue value;
            try
            {
                key = Policy.KeyCodec.Decode(keyBytes);
                value = Policy.ValueCodec.Decode(valueBytes);
            }
            catch (FormatException exception)
            {
                throw Verification(MerkleVerificationFailureKind.MalformedBlock, "A Merkle codec rejected serialized entry bytes.", block.Digest, exception);
            }
            if (!EncodeOwned(Policy.KeyCodec, key).AsSpan().SequenceEqual(keyBytes)
                || !EncodeOwned(Policy.ValueCodec, value).AsSpan().SequenceEqual(valueBytes))
            {
                throw Verification(MerkleVerificationFailureKind.NonCanonicalBlock, "A decoded entry does not round-trip to the exact encoded bytes.", block.Digest);
            }
            var entryLayer = Layer(Policy.HashKey(keyBytes));
            if (entryLayer != level)
                throw Verification(MerkleVerificationFailureKind.NonCanonicalBlock, "An entry is stored at a layer other than its hash-derived layer.", block.Digest);
            entries[index] = new EntryRecord(key, value, keyBytes, valueBytes, entryLayer);
            if (index != 0 && Policy.Comparer.Compare(entries[index - 1].Key, key) >= 0)
                throw Verification(MerkleVerificationFailureKind.NonCanonicalBlock, "Merkle block entries are not strictly comparer-ordered.", block.Digest);
        }

        var children = new MerkleDigest[entryCount + 1];
        for (var index = 0; index < children.Length; index++)
            children[index] = MerkleDigest.FromBytes(Take(bytes, ref offset, DigestLength));
        if (offset != bytes.Length)
            throw Verification(MerkleVerificationFailureKind.NonCanonicalBlock, "A Merkle block has trailing bytes.", block.Digest);

        var canonical = EncodeRawBlock(level, subtreeCount, entries, children);
        if (!canonical.AsSpan().SequenceEqual(bytes))
            throw Verification(MerkleVerificationFailureKind.NonCanonicalBlock, "A Merkle block is not the unique canonical encoding.", block.Digest);
        return new DecodedBlock(block, level, subtreeCount, entries, children);
    }

    private byte[] EncodeRawBlock(
        int level,
        int subtreeCount,
        EntryRecord[] entries,
        MerkleDigest[] childDigests)
    {
        var length = checked(BlockHeaderLength + checked(childDigests.Length * DigestLength));
        foreach (var entry in entries)
            length = checked(length + sizeof(int) + entry.KeyBytes.Length + sizeof(int) + entry.ValueBytes.Length);
        var result = new byte[length];
        var offset = 0;
        BlockMagic.CopyTo(result, offset);
        offset += BlockMagic.Length;
        result[offset++] = NodeBlockTag;
        Policy.DomainDigest.WriteBytes(result.AsSpan(offset, DigestLength));
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
        foreach (var childDigest in childDigests)
        {
            childDigest.WriteBytes(result.AsSpan(offset, DigestLength));
            offset += DigestLength;
        }
        return result;
    }

    private static int ReadInt32(ReadOnlySpan<byte> source, ref int offset)
    {
        var value = BinaryPrimitives.ReadInt32BigEndian(Take(source, ref offset, sizeof(int)));
        return value;
    }

    private static byte[] ReadLengthPrefixed(ReadOnlySpan<byte> source, ref int offset, MerkleDigest digest)
    {
        var length = ReadInt32(source, ref offset);
        if (length < 0)
            throw Verification(MerkleVerificationFailureKind.MalformedBlock, "A length-prefixed field has a negative length.", digest);
        return Take(source, ref offset, length).ToArray();
    }

    private static ReadOnlySpan<byte> Take(ReadOnlySpan<byte> source, ref int offset, int length)
    {
        if (length < 0 || offset < 0 || length > source.Length - offset)
            throw Verification(MerkleVerificationFailureKind.MalformedBlock, "A serialized Merkle field is truncated.");
        var result = source.Slice(offset, length);
        offset += length;
        return result;
    }

    private static byte[] EncodePointQuery(MerkleProofKind kind, byte[] keyBytes, byte[]? valueBytes)
    {
        if (kind == MerkleProofKind.Membership && valueBytes is null)
            throw new InvalidOperationException("A membership proof must carry canonical value bytes.");
        if (kind == MerkleProofKind.NonMembership && valueBytes is not null)
            throw new InvalidOperationException("A non-membership proof must not carry value bytes.");
        var result = new byte[
            ProofMagic.Length
            + 1
            + sizeof(int)
            + keyBytes.Length
            + (valueBytes is null ? 0 : sizeof(int) + valueBytes.Length)];
        var offset = 0;
        ProofMagic.CopyTo(result, offset);
        offset += ProofMagic.Length;
        result[offset++] = checked((byte)kind);
        WriteQueryField(result, ref offset, keyBytes);
        if (valueBytes is not null)
            WriteQueryField(result, ref offset, valueBytes);
        return result;
    }

    private static byte[] EncodeRangeQuery(byte[] minimumBytes, byte[] maximumBytes)
    {
        var result = new byte[ProofMagic.Length + 1 + sizeof(int) * 2 + minimumBytes.Length + maximumBytes.Length];
        var offset = 0;
        ProofMagic.CopyTo(result, offset);
        offset += ProofMagic.Length;
        result[offset++] = (byte)MerkleProofKind.Range;
        WriteQueryField(result, ref offset, minimumBytes);
        WriteQueryField(result, ref offset, maximumBytes);
        return result;
    }

    private static void WriteQueryField(byte[] destination, ref int offset, byte[] field)
    {
        BinaryPrimitives.WriteInt32BigEndian(destination.AsSpan(offset, sizeof(int)), field.Length);
        offset += sizeof(int);
        field.CopyTo(destination, offset);
        offset += field.Length;
    }

    private void CollectRangeProof(
        Node? node,
        TKey minimumKey,
        TKey maximumKey,
        List<MerkleProofStep> steps)
    {
        if (node is null)
            return;
        var expanded = new List<int>();
        for (var index = 0; index < node.Children.Length; index++)
        {
            if (node.Children[index] is not null && ChildIntervalIntersects(node.Entries, index, minimumKey, maximumKey))
                expanded.Add(index);
        }
        steps.Add(new MerkleProofStep(new MerkleBlock(node.Digest, node.BlockBytes), expanded));
        foreach (var index in expanded)
            CollectRangeProof(node.Children[index], minimumKey, maximumKey, steps);
    }

    private bool ChildIntervalIntersects(
        EntryRecord[] entries,
        int childIndex,
        TKey minimumKey,
        TKey maximumKey) =>
        (childIndex == 0 || Policy.Comparer.Compare(entries[childIndex - 1].Key, maximumKey) < 0)
        && (childIndex == entries.Length || Policy.Comparer.Compare(entries[childIndex].Key, minimumKey) > 0);

    private void VerifyPointProof(
        MerkleProof proof,
        IReadOnlyDictionary<MerkleDigest, DecodedBlock> decoded,
        IReadOnlyDictionary<MerkleDigest, MerkleProofStep> steps,
        HashSet<MerkleDigest> visited,
        VerificationContext context)
    {
        var query = DecodePointQuery(proof);
        var key = query.Key;
        var digest = proof.RootHash;
        var depth = 1;
        while (true)
        {
            if (!visited.Add(digest))
                throw Verification(MerkleVerificationFailureKind.CycleDetected, "A proof expansion contains a cycle.", digest);
            if (!decoded.TryGetValue(digest, out var block) || !steps.TryGetValue(digest, out var step))
                throw Verification(MerkleVerificationFailureKind.MissingBlock, "A proof omits an expanded point-query block.", digest);
            context.CheckDepth(depth, digest);
            var position = FindDecodedPosition(block.Entries, key, out var found);
            if (found)
            {
                RequireExpandedExactly(step, []);
                if (proof.Kind != MerkleProofKind.Membership)
                    throw Verification(MerkleVerificationFailureKind.ProofMismatch, "The proof claims absence for a present key.", digest);
                if (query.ValueBytes is null
                    || !block.Entries[position].ValueBytes.AsSpan().SequenceEqual(query.ValueBytes))
                {
                    throw Verification(MerkleVerificationFailureKind.ProofMismatch, "The membership proof's value claim does not match the authenticated entry.", digest);
                }
                return;
            }

            var childDigest = block.ChildDigests[position];
            if (childDigest == Policy.EmptyDigest)
            {
                RequireExpandedExactly(step, []);
                if (proof.Kind != MerkleProofKind.NonMembership)
                    throw Verification(MerkleVerificationFailureKind.ProofMismatch, "The proof claims membership for an absent key.", digest);
                return;
            }
            RequireExpandedExactly(step, [position]);
            if (!decoded.TryGetValue(childDigest, out var child))
                throw Verification(MerkleVerificationFailureKind.MissingBlock, "A proof omits its expanded child block.", childDigest);
            ValidateProofReference(block, position, child);
            digest = childDigest;
            depth = checked(depth + 1);
        }
    }

    private void VerifyRangeProof(
        MerkleProof proof,
        IReadOnlyDictionary<MerkleDigest, DecodedBlock> decoded,
        IReadOnlyDictionary<MerkleDigest, MerkleProofStep> steps,
        HashSet<MerkleDigest> visited,
        VerificationContext context)
    {
        var (minimumKey, maximumKey) = DecodeRangeQuery(proof);
        VerifyRangeBlock(proof.RootHash, minimumKey, maximumKey, decoded, steps, visited, context, 1);
    }

    private void VerifyRangeBlock(
        MerkleDigest digest,
        TKey minimumKey,
        TKey maximumKey,
        IReadOnlyDictionary<MerkleDigest, DecodedBlock> decoded,
        IReadOnlyDictionary<MerkleDigest, MerkleProofStep> steps,
        HashSet<MerkleDigest> visited,
        VerificationContext context,
        int depth)
    {
        context.CheckDepth(depth, digest);
        if (!visited.Add(digest))
            throw Verification(MerkleVerificationFailureKind.CycleDetected, "A range-proof expansion contains a repeated block.", digest);
        if (!decoded.TryGetValue(digest, out var block) || !steps.TryGetValue(digest, out var step))
            throw Verification(MerkleVerificationFailureKind.MissingBlock, "A range proof omits an expanded block.", digest);

        var expected = new List<int>();
        for (var index = 0; index < block.ChildDigests.Length; index++)
        {
            if (block.ChildDigests[index] != Policy.EmptyDigest
                && ChildIntervalIntersects(block.Entries, index, minimumKey, maximumKey))
            {
                expected.Add(index);
            }
        }
        RequireExpandedExactly(step, expected);
        foreach (var index in expected)
        {
            var childDigest = block.ChildDigests[index];
            if (!decoded.TryGetValue(childDigest, out var child))
                throw Verification(MerkleVerificationFailureKind.MissingBlock, "A range proof omits an expanded child block.", childDigest);
            ValidateProofReference(block, index, child);
            VerifyRangeBlock(childDigest, minimumKey, maximumKey, decoded, steps, visited, context, checked(depth + 1));
        }
    }

    private void ValidateProofReference(DecodedBlock parent, int childIndex, DecodedBlock child)
    {
        if (child.Level >= parent.Level)
            throw Verification(MerkleVerificationFailureKind.InvalidReference, "An expanded proof child is not below its parent layer.", child.Block.Digest);
        foreach (var entry in child.Entries)
        {
            if (childIndex != 0 && Policy.Comparer.Compare(entry.Key, parent.Entries[childIndex - 1].Key) <= 0)
                throw Verification(MerkleVerificationFailureKind.InvalidReference, "An expanded proof child crosses its lower separator.", child.Block.Digest);
            if (childIndex != parent.Entries.Length && Policy.Comparer.Compare(entry.Key, parent.Entries[childIndex].Key) >= 0)
                throw Verification(MerkleVerificationFailureKind.InvalidReference, "An expanded proof child crosses its upper separator.", child.Block.Digest);
        }
    }

    private int FindDecodedPosition(EntryRecord[] entries, TKey key, out bool found) =>
        FindPosition(entries, key, out found);

    private static void RequireExpandedExactly(MerkleProofStep step, IReadOnlyList<int> expected)
    {
        if (step.ExpandedChildIndexes.Count != expected.Count)
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A proof step expands a noncanonical set of child intervals.", step.Block.Digest);
        for (var index = 0; index < expected.Count; index++)
        {
            if (step.ExpandedChildIndexes[index] != expected[index])
                throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A proof step expands a noncanonical set of child intervals.", step.Block.Digest);
        }
    }

    private (TKey Key, byte[]? ValueBytes) DecodePointQuery(MerkleProof proof)
    {
        if (proof.Kind is not (MerkleProofKind.Membership or MerkleProofKind.NonMembership))
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A point proof has the wrong proof kind.", proof.RootHash);
        var query = proof.Query;
        var offset = ReadQueryHeader(query, proof.Kind);
        var keyBytes = ReadQueryField(query, ref offset, proof.RootHash);
        byte[]? valueBytes = null;
        if (proof.Kind == MerkleProofKind.Membership)
        {
            valueBytes = ReadQueryField(query, ref offset, proof.RootHash).ToArray();
            TValue value;
            try
            {
                value = Policy.ValueCodec.Decode(valueBytes);
            }
            catch (FormatException exception)
            {
                throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A membership query contains an invalid value encoding.", proof.RootHash, exception);
            }
            if (!EncodeOwned(Policy.ValueCodec, value).AsSpan().SequenceEqual(valueBytes))
                throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A membership query value is not canonically encoded.", proof.RootHash);
        }
        if (offset != query.Length)
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A point-proof query has trailing bytes.", proof.RootHash);
        return (DecodeCanonicalQueryKey(keyBytes, proof.RootHash), valueBytes);
    }

    private (TKey Minimum, TKey Maximum) DecodeRangeQuery(MerkleProof proof)
    {
        if (proof.Kind != MerkleProofKind.Range)
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A range proof has the wrong proof kind.", proof.RootHash);
        var query = proof.Query;
        var offset = ReadQueryHeader(query, MerkleProofKind.Range);
        var minimumBytes = ReadQueryField(query, ref offset, proof.RootHash);
        var maximumBytes = ReadQueryField(query, ref offset, proof.RootHash);
        if (offset != query.Length)
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A range-proof query has trailing bytes.", proof.RootHash);
        var minimum = DecodeCanonicalQueryKey(minimumBytes, proof.RootHash);
        var maximum = DecodeCanonicalQueryKey(maximumBytes, proof.RootHash);
        if (Policy.Comparer.Compare(minimum, maximum) > 0)
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A range-proof query has reversed bounds.", proof.RootHash);
        return (minimum, maximum);
    }

    private TKey DecodeCanonicalQueryKey(ReadOnlySpan<byte> bytes, MerkleDigest rootHash)
    {
        TKey key;
        try
        {
            key = Policy.KeyCodec.Decode(bytes);
        }
        catch (FormatException exception)
        {
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A proof query contains an invalid key encoding.", rootHash, exception);
        }
        if (!EncodeOwned(Policy.KeyCodec, key).AsSpan().SequenceEqual(bytes))
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A proof query key is not canonically encoded.", rootHash);
        return key;
    }

    private static int ReadQueryHeader(ReadOnlySpan<byte> query, MerkleProofKind expectedKind)
    {
        var offset = 0;
        if (!Take(query, ref offset, ProofMagic.Length).SequenceEqual(ProofMagic))
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A proof query has the wrong magic.");
        if (Take(query, ref offset, 1)[0] != (byte)expectedKind)
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A proof query kind disagrees with its envelope.");
        return offset;
    }

    private static ReadOnlySpan<byte> ReadQueryField(ReadOnlySpan<byte> query, ref int offset, MerkleDigest rootHash)
    {
        var length = ReadInt32(query, ref offset);
        if (length < 0)
            throw Verification(MerkleVerificationFailureKind.ProofMismatch, "A proof query field has a negative length.", rootHash);
        return Take(query, ref offset, length);
    }

    private static void VerifyEmptyProofQuery(
        MerkleProof proof,
        MerkleSearchTreePolicy<TKey, TValue> policy)
    {
        var verifier = Create(policy);
        switch (proof.Kind)
        {
            case MerkleProofKind.NonMembership:
                _ = verifier.DecodePointQuery(proof);
                break;
            case MerkleProofKind.Range:
                _ = verifier.DecodeRangeQuery(proof);
                break;
            default:
                throw Verification(MerkleVerificationFailureKind.ProofMismatch, "An empty tree cannot prove membership.", proof.RootHash);
        }
    }

    private static TKey MinimumKey(
        IComparer<TKey> comparer,
        KeyValuePair<TKey, TValue>[] baseEntries,
        int baseIndex,
        KeyValuePair<TKey, TValue>[] leftEntries,
        int leftIndex,
        KeyValuePair<TKey, TValue>[] rightEntries,
        int rightIndex)
    {
        var hasKey = false;
        var key = default(TKey)!;
        if (baseIndex < baseEntries.Length)
        {
            key = baseEntries[baseIndex].Key;
            hasKey = true;
        }
        if (leftIndex < leftEntries.Length && (!hasKey || comparer.Compare(leftEntries[leftIndex].Key, key) < 0))
        {
            key = leftEntries[leftIndex].Key;
            hasKey = true;
        }
        if (rightIndex < rightEntries.Length && (!hasKey || comparer.Compare(rightEntries[rightIndex].Key, key) < 0))
            key = rightEntries[rightIndex].Key;
        return key;
    }

    private static MergeSide TakeSide(
        IComparer<TKey> comparer,
        KeyValuePair<TKey, TValue>[] entries,
        ref int index,
        TKey key)
    {
        if (index >= entries.Length || comparer.Compare(entries[index].Key, key) != 0)
            return default;
        var entry = entries[index++];
        return new MergeSide(entry.Key, MerkleMergeValue<TValue>.Present(entry.Value));
    }

    private static bool MergeValuesEqual(
        MerkleMergeValue<TValue> left,
        MerkleMergeValue<TValue> right,
        IEqualityComparer<TValue> comparer) =>
        left.HasValue == right.HasValue
        && (!left.HasValue || comparer.Equals(left.Value, right.Value));

    private static void AppendMergeSide(
        List<KeyValuePair<TKey, TValue>> output,
        MergeSide selected,
        MergeSide alternate,
        MergeSide fallback)
    {
        if (!selected.Value.HasValue)
            return;
        var representative = selected.HasKey
            ? selected.Key
            : alternate.HasKey
                ? alternate.Key
                : fallback.Key;
        output.Add(KeyValuePair.Create(representative, selected.Value.Value));
    }

    private static bool ApplyResolution(
        List<KeyValuePair<TKey, TValue>> output,
        TKey conflictKey,
        MerkleMergeResolution<TValue> resolution,
        MergeSide baseSide,
        MergeSide leftSide,
        MergeSide rightSide)
    {
        switch (resolution.Kind)
        {
            case MerkleMergeResolutionKind.Unresolved:
                return false;
            case MerkleMergeResolutionKind.UseBase:
                AppendMergeSide(output, baseSide, leftSide, rightSide);
                return true;
            case MerkleMergeResolutionKind.UseLeft:
                AppendMergeSide(output, leftSide, rightSide, baseSide);
                return true;
            case MerkleMergeResolutionKind.UseRight:
                AppendMergeSide(output, rightSide, leftSide, baseSide);
                return true;
            case MerkleMergeResolutionKind.Delete:
                return true;
            case MerkleMergeResolutionKind.SetValue:
                if (!resolution.Value.HasValue)
                    throw new InvalidOperationException("A SetValue merge resolution must carry a present value.");
                output.Add(KeyValuePair.Create(conflictKey, resolution.Value.Value));
                return true;
            default:
                throw new ArgumentOutOfRangeException(nameof(resolution), "The merge resolver returned an unknown resolution kind.");
        }
    }

    private static void VerifyEnvelope(
        string algorithmId,
        MerkleDigest domainDigest,
        MerkleSearchTreePolicy<TKey, TValue> policy)
    {
        if (!string.Equals(algorithmId, MerkleSearchTreePolicy<TKey, TValue>.AlgorithmId, StringComparison.Ordinal))
            throw Verification(MerkleVerificationFailureKind.UnsupportedAlgorithm, $"Merkle algorithm '{algorithmId}' is not supported.");
        if (domainDigest != policy.DomainDigest)
            throw Verification(MerkleVerificationFailureKind.DomainMismatch, "The Merkle envelope belongs to another policy domain.");
    }

    private static MerkleVerificationException Verification(
        MerkleVerificationFailureKind kind,
        string message,
        MerkleDigest? digest = null,
        Exception? innerException = null) =>
        new(kind, message, digest, innerException);

    private sealed class DecodedBlock(
        MerkleBlock block,
        int level,
        int subtreeCount,
        EntryRecord[] entries,
        MerkleDigest[] childDigests)
    {
        internal MerkleBlock Block { get; } = block;
        internal int Level { get; } = level;
        internal int SubtreeCount { get; } = subtreeCount;
        internal EntryRecord[] Entries { get; } = entries;
        internal MerkleDigest[] ChildDigests { get; } = childDigests;
    }

    private readonly struct MergeSide
    {
        internal MergeSide(TKey key, MerkleMergeValue<TValue> value)
        {
            HasKey = true;
            Key = key;
            Value = value;
        }

        internal bool HasKey { get; }
        internal TKey Key { get; }
        internal MerkleMergeValue<TValue> Value { get; }
    }

    private sealed class VerificationContext(MerkleVerificationBudget budget)
    {
        internal MerkleVerificationBudget Budget { get; } = budget;
        internal int BlockCount { get; private set; }
        internal long TotalBytes { get; private set; }
        private long _entryCount;
        private readonly HashSet<MerkleDigest> _blocks = [];

        internal void CheckDepth(int depth, MerkleDigest? digest = null)
        {
            if (depth <= 0 || depth > Budget.MaxDepth)
                throw Verification(MerkleVerificationFailureKind.ResourceLimitExceeded, "Merkle verification exceeded the maximum reference depth.", digest);
        }

        internal bool Account(MerkleBlock block, int depth)
        {
            CheckDepth(depth, block.Digest);
            if (!_blocks.Add(block.Digest))
                return false;
            if (block.Length > Budget.MaxBlockByteCount)
                throw Verification(MerkleVerificationFailureKind.ResourceLimitExceeded, "A Merkle block exceeds the per-block byte budget.", block.Digest);
            if (BlockCount >= Budget.MaxBlockCount || TotalBytes > Budget.MaxTotalByteCount - block.Length)
                throw Verification(MerkleVerificationFailureKind.ResourceLimitExceeded, "Merkle verification exceeded its block or total-byte budget.", block.Digest);
            BlockCount++;
            TotalBytes += block.Length;
            return true;
        }

        internal void AccountProofQuery(int byteCount, MerkleDigest rootHash)
        {
            if (byteCount > Budget.MaxProofQueryByteCount
                || TotalBytes > Budget.MaxTotalByteCount - byteCount)
            {
                throw Verification(
                    MerkleVerificationFailureKind.ResourceLimitExceeded,
                    "A Merkle proof query exceeds its query or total-byte budget.",
                    rootHash);
            }
            TotalBytes += byteCount;
        }

        internal void PreflightProofStructure(MerkleProof proof)
        {
            if (proof.Steps.Count > Budget.MaxBlockCount)
            {
                throw Verification(
                    MerkleVerificationFailureKind.ResourceLimitExceeded,
                    "A Merkle proof exceeds its block-count budget.",
                    proof.RootHash);
            }

            foreach (var step in proof.Steps)
            {
                if (step.ExpandedChildIndexes.Count > Budget.MaxChildReferencesPerBlock)
                {
                    throw Verification(
                        MerkleVerificationFailureKind.ResourceLimitExceeded,
                        "A Merkle proof step exceeds its expanded-child budget.",
                        step.Block.Digest);
                }
            }
        }

        internal void AccountEntries(int count, MerkleDigest digest)
        {
            if (_entryCount > Budget.MaxEntryCount - count)
                throw Verification(MerkleVerificationFailureKind.ResourceLimitExceeded, "Merkle verification exceeded its decoded-entry budget.", digest);
            _entryCount += count;
        }
    }

    private sealed class OverlayBlockStore(
        IReadOnlyDictionary<MerkleDigest, MerkleBlock> staged,
        IMerkleBlockStore? fallback) : IMerkleBlockStore
    {
        public int Count => staged.Count + (fallback?.Count ?? 0);

        public IReadOnlyCollection<MerkleDigest> Digests
        {
            get
            {
                var result = new HashSet<MerkleDigest>(staged.Keys);
                if (fallback is not null)
                    result.UnionWith(fallback.Digests);
                var array = result.ToArray();
                Array.Sort(array);
                return Array.AsReadOnly(array);
            }
        }

        public bool Contains(MerkleDigest digest) =>
            staged.ContainsKey(digest) || (fallback?.Contains(digest) ?? false);

        public bool TryGet(MerkleDigest digest, [NotNullWhen(true)] out MerkleBlock? block)
        {
            if (staged.TryGetValue(digest, out block))
                return true;
            if (fallback is not null)
                return fallback.TryGet(digest, out block);
            block = null;
            return false;
        }

        public bool Put(MerkleBlock block) => throw new NotSupportedException("The verification overlay is read-only.");

        public bool Remove(MerkleDigest digest) => throw new NotSupportedException("The verification overlay is read-only.");

        public void Clear() => throw new NotSupportedException("The verification overlay is read-only.");
    }
}
