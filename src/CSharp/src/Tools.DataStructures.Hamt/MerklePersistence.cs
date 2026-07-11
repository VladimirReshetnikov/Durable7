using System.Collections.Concurrent;
using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.Hamt;

/// <summary>Represents one immutable, digest-addressed serialized Merkle block.</summary>
/// <remarks>
/// The constructor takes ownership by copying its content input. It does not recompute the supplied
/// digest because the exact hash preimage is defined by the versioned Merkle
/// format rather than by the storage layer. A verified loader must validate the pair before using
/// the block as trusted tree state.
/// </remarks>
public sealed class MerkleBlock : IEquatable<MerkleBlock>
{
    private readonly byte[] _content;

    /// <summary>Initializes an immutable Merkle block.</summary>
    /// <param name="digest">The content address assigned by the versioned Merkle format.</param>
    /// <param name="content">The complete canonical serialized block.</param>
    public MerkleBlock(MerkleDigest digest, ReadOnlySpan<byte> content)
    {
        Digest = digest;
        _content = content.ToArray();
    }

    /// <summary>Gets the content address.</summary>
    public MerkleDigest Digest { get; }

    /// <summary>Gets a read-only view of the canonical serialized block.</summary>
    /// <remarks>The returned span remains valid for the lifetime of this instance.</remarks>
    public ReadOnlySpan<byte> Content => _content;

    /// <summary>Gets the serialized block length in bytes.</summary>
    public int Length => _content.Length;

    /// <summary>Returns a newly owned copy of the serialized block.</summary>
    /// <returns>A byte array that the caller may mutate.</returns>
    public byte[] ToArray() => [.. _content];

    /// <inheritdoc/>
    public bool Equals(MerkleBlock? other) =>
        other is not null
        && Digest == other.Digest
        && _content.AsSpan().SequenceEqual(other._content);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is MerkleBlock other && Equals(other);

    /// <inheritdoc/>
    public override int GetHashCode() => Digest.GetHashCode();

    /// <inheritdoc/>
    public override string ToString() => $"{Digest} ({Length} bytes)";
}

/// <summary>Stores immutable Merkle blocks under their content digests.</summary>
/// <remarks>
/// Implementations must treat a second write of identical bytes under the same digest as an
/// idempotent no-op and must reject different bytes under an existing digest. The store is not a
/// substitute for a format-aware verifier: loaders remain responsible for recomputing digests,
/// decoding canonical bytes, checking child references, and enforcing resource budgets.
/// </remarks>
public interface IMerkleBlockStore
{
    /// <summary>Gets the number of stored blocks.</summary>
    int Count { get; }

    /// <summary>Gets a stable snapshot of the stored digests.</summary>
    IReadOnlyCollection<MerkleDigest> Digests { get; }

    /// <summary>Determines whether a block is present.</summary>
    /// <param name="digest">The requested digest.</param>
    /// <returns><see langword="true"/> when the block is present.</returns>
    bool Contains(MerkleDigest digest);

    /// <summary>Tries to retrieve a block.</summary>
    /// <param name="digest">The requested digest.</param>
    /// <param name="block">The immutable block when present.</param>
    /// <returns><see langword="true"/> when the block is present.</returns>
    bool TryGet(MerkleDigest digest, [NotNullWhen(true)] out MerkleBlock? block);

    /// <summary>Stores a block idempotently.</summary>
    /// <param name="block">The immutable block to store.</param>
    /// <returns><see langword="true"/> when the block was added; <see langword="false"/> when identical content was already present.</returns>
    /// <exception cref="MerkleVerificationException">Different bytes are already stored under the same digest.</exception>
    bool Put(MerkleBlock block);

    /// <summary>Removes a block when present.</summary>
    /// <param name="digest">The digest to remove.</param>
    /// <returns><see langword="true"/> when a block was removed.</returns>
    bool Remove(MerkleDigest digest);

    /// <summary>Removes all stored blocks.</summary>
    void Clear();
}

/// <summary>Provides a thread-safe in-memory Merkle block store.</summary>
/// <remarks>
/// Blocks are immutable, reads and idempotent writes are safe from concurrent threads, and
/// <see cref="Digests"/> returns a sorted point-in-time snapshot. This implementation is intended
/// for tests, local synchronization, and ephemeral caches; it provides no durable transaction or
/// eviction policy.
/// </remarks>
public sealed class InMemoryMerkleBlockStore : IMerkleBlockStore
{
    private readonly ConcurrentDictionary<MerkleDigest, MerkleBlock> _blocks = new();

    /// <inheritdoc/>
    public int Count => _blocks.Count;

    /// <inheritdoc/>
    public IReadOnlyCollection<MerkleDigest> Digests
    {
        get
        {
            var digests = _blocks.Keys.ToArray();
            Array.Sort(digests);
            return Array.AsReadOnly(digests);
        }
    }

    /// <inheritdoc/>
    public bool Contains(MerkleDigest digest) => _blocks.ContainsKey(digest);

    /// <inheritdoc/>
    public bool TryGet(MerkleDigest digest, [NotNullWhen(true)] out MerkleBlock? block) =>
        _blocks.TryGetValue(digest, out block);

    /// <inheritdoc/>
    public bool Put(MerkleBlock block)
    {
        ArgumentNullException.ThrowIfNull(block);
        while (true)
        {
            if (_blocks.TryAdd(block.Digest, block))
                return true;
            if (!_blocks.TryGetValue(block.Digest, out var existing))
                continue;
            if (existing.Equals(block))
                return false;
            throw new MerkleVerificationException(
                MerkleVerificationFailureKind.ConflictingBlock,
                $"Digest '{block.Digest}' is already associated with different block bytes.",
                block.Digest);
        }
    }

    /// <inheritdoc/>
    public bool Remove(MerkleDigest digest) => _blocks.TryRemove(digest, out _);

    /// <inheritdoc/>
    public void Clear() => _blocks.Clear();
}

/// <summary>Represents an immutable transfer pack of uniquely addressed Merkle blocks.</summary>
/// <remarks>
/// A pack may be complete or partial. In particular, synchronization may send only the blocks
/// requested in one round, so <see cref="ContainsRootBlock"/> is diagnostic rather than an
/// invariant. Construction preserves block order and rejects duplicate digests.
/// </remarks>
public sealed record MerkleBlockPack
{
    /// <summary>Initializes a Merkle block pack.</summary>
    /// <param name="algorithmId">The stable versioned identifier of the block format and hashing algorithm.</param>
    /// <param name="domainDigest">The policy and codec domain fingerprint.</param>
    /// <param name="rootHash">The root hash whose closure the pack wholly or partially describes.</param>
    /// <param name="blocks">The uniquely addressed blocks in transfer order.</param>
    public MerkleBlockPack(
        string algorithmId,
        MerkleDigest domainDigest,
        MerkleDigest rootHash,
        IEnumerable<MerkleBlock> blocks)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(algorithmId);
        ArgumentNullException.ThrowIfNull(blocks);

        var materialized = blocks.ToArray();
        var digests = new HashSet<MerkleDigest>();
        long totalByteCount = 0;
        var containsRoot = false;
        foreach (var block in materialized)
        {
            ArgumentNullException.ThrowIfNull(block);
            if (!digests.Add(block.Digest))
                throw new ArgumentException($"The pack contains duplicate digest '{block.Digest}'.", nameof(blocks));
            totalByteCount = checked(totalByteCount + block.Length);
            containsRoot |= block.Digest == rootHash;
        }

        AlgorithmId = algorithmId;
        DomainDigest = domainDigest;
        RootHash = rootHash;
        Blocks = Array.AsReadOnly(materialized);
        TotalByteCount = totalByteCount;
        ContainsRootBlock = containsRoot;
    }

    /// <summary>Gets the versioned block-format and hashing identifier.</summary>
    public string AlgorithmId { get; }

    /// <summary>Gets the policy and codec domain fingerprint.</summary>
    public MerkleDigest DomainDigest { get; }

    /// <summary>Gets the root whose closure the pack wholly or partially describes.</summary>
    public MerkleDigest RootHash { get; }

    /// <summary>Gets the blocks in transfer order.</summary>
    public IReadOnlyList<MerkleBlock> Blocks { get; }

    /// <summary>Gets the number of blocks.</summary>
    public int BlockCount => Blocks.Count;

    /// <summary>Gets the total serialized payload size.</summary>
    public long TotalByteCount { get; }

    /// <summary>Gets whether this pack directly includes the named root block.</summary>
    public bool ContainsRootBlock { get; }

    /// <inheritdoc/>
    public bool Equals(MerkleBlockPack? other) =>
        ReferenceEquals(this, other)
        || (other is not null
            && string.Equals(AlgorithmId, other.AlgorithmId, StringComparison.Ordinal)
            && DomainDigest == other.DomainDigest
            && RootHash == other.RootHash
            && Blocks.SequenceEqual(other.Blocks));

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var hash = new HashCode();
        hash.Add(AlgorithmId, StringComparer.Ordinal);
        hash.Add(DomainDigest);
        hash.Add(RootHash);
        foreach (var block in Blocks)
            hash.Add(block);
        return hash.ToHashCode();
    }
}

/// <summary>Describes one immutable round of block-level Merkle synchronization.</summary>
/// <remarks>
/// The requested digests are the blocks the local side still needs from the peer before it can
/// continue or complete verification of <see cref="TargetRootHash"/>. A caller may fetch them as a
/// <see cref="MerkleBlockPack"/>, add the verified blocks to a store, and compute a new plan.
/// </remarks>
public sealed record MerkleSyncPlan
{
    /// <summary>Initializes a synchronization plan.</summary>
    /// <param name="algorithmId">The stable versioned block-format and hashing identifier.</param>
    /// <param name="domainDigest">The policy and codec domain fingerprint.</param>
    /// <param name="localRootHash">The current local root hash.</param>
    /// <param name="targetRootHash">The peer root hash being synchronized.</param>
    /// <param name="requestedBlocks">The unique missing block digests in request order.</param>
    /// <param name="examinedBlockCount">The number of blocks examined while producing the plan.</param>
    /// <param name="examinedByteCount">The number of serialized bytes examined while producing the plan.</param>
    public MerkleSyncPlan(
        string algorithmId,
        MerkleDigest domainDigest,
        MerkleDigest localRootHash,
        MerkleDigest targetRootHash,
        IEnumerable<MerkleDigest> requestedBlocks,
        int examinedBlockCount,
        long examinedByteCount)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(algorithmId);
        ArgumentNullException.ThrowIfNull(requestedBlocks);
        ArgumentOutOfRangeException.ThrowIfNegative(examinedBlockCount);
        ArgumentOutOfRangeException.ThrowIfNegative(examinedByteCount);

        var materialized = requestedBlocks.ToArray();
        var unique = new HashSet<MerkleDigest>();
        foreach (var digest in materialized)
        {
            if (!unique.Add(digest))
                throw new ArgumentException($"The plan requests digest '{digest}' more than once.", nameof(requestedBlocks));
        }

        AlgorithmId = algorithmId;
        DomainDigest = domainDigest;
        LocalRootHash = localRootHash;
        TargetRootHash = targetRootHash;
        RequestedBlocks = Array.AsReadOnly(materialized);
        ExaminedBlockCount = examinedBlockCount;
        ExaminedByteCount = examinedByteCount;
    }

    /// <summary>Gets the versioned block-format and hashing identifier.</summary>
    public string AlgorithmId { get; }

    /// <summary>Gets the policy and codec domain fingerprint.</summary>
    public MerkleDigest DomainDigest { get; }

    /// <summary>Gets the current local root.</summary>
    public MerkleDigest LocalRootHash { get; }

    /// <summary>Gets the peer root being synchronized.</summary>
    public MerkleDigest TargetRootHash { get; }

    /// <summary>Gets the unique missing block digests in request order.</summary>
    public IReadOnlyList<MerkleDigest> RequestedBlocks { get; }

    /// <summary>Gets the number of blocks examined while producing this plan.</summary>
    public int ExaminedBlockCount { get; }

    /// <summary>Gets the number of serialized bytes examined while producing this plan.</summary>
    public long ExaminedByteCount { get; }

    /// <summary>Gets whether the local and target roots already match.</summary>
    public bool RootsMatch => LocalRootHash == TargetRootHash;

    /// <summary>Gets whether another block-transfer round is required.</summary>
    public bool RequiresBlocks => RequestedBlocks.Count != 0;

    /// <inheritdoc/>
    public bool Equals(MerkleSyncPlan? other) =>
        ReferenceEquals(this, other)
        || (other is not null
            && string.Equals(AlgorithmId, other.AlgorithmId, StringComparison.Ordinal)
            && DomainDigest == other.DomainDigest
            && LocalRootHash == other.LocalRootHash
            && TargetRootHash == other.TargetRootHash
            && ExaminedBlockCount == other.ExaminedBlockCount
            && ExaminedByteCount == other.ExaminedByteCount
            && RequestedBlocks.SequenceEqual(other.RequestedBlocks));

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var hash = new HashCode();
        hash.Add(AlgorithmId, StringComparer.Ordinal);
        hash.Add(DomainDigest);
        hash.Add(LocalRootHash);
        hash.Add(TargetRootHash);
        hash.Add(ExaminedBlockCount);
        hash.Add(ExaminedByteCount);
        foreach (var digest in RequestedBlocks)
            hash.Add(digest);
        return hash.ToHashCode();
    }
}

/// <summary>Identifies the claim carried by a Merkle proof.</summary>
public enum MerkleProofKind
{
    /// <summary>The proof claims that one encoded key and value are present.</summary>
    Membership,

    /// <summary>The proof claims that one encoded key is absent.</summary>
    NonMembership,

    /// <summary>The proof claims completeness and contents for an encoded key range.</summary>
    Range,
}

/// <summary>Represents one immutable serialized block participating in a Merkle proof.</summary>
/// <remarks>
/// <see cref="ExpandedChildIndexes"/> identifies child references whose blocks are also supplied by
/// the proof. Other child digests remain authenticated opaque boundaries. A membership proof
/// normally expands one child at each level; a range proof may expand several.
/// </remarks>
public sealed record MerkleProofStep
{
    /// <summary>Initializes a proof step.</summary>
    /// <param name="block">The complete canonical serialized block.</param>
    /// <param name="expandedChildIndexes">The unique non-negative child indexes expanded by other proof steps.</param>
    public MerkleProofStep(MerkleBlock block, IEnumerable<int> expandedChildIndexes)
    {
        ArgumentNullException.ThrowIfNull(block);
        ArgumentNullException.ThrowIfNull(expandedChildIndexes);

        var indexes = expandedChildIndexes.ToArray();
        var unique = new HashSet<int>();
        foreach (var index in indexes)
        {
            ArgumentOutOfRangeException.ThrowIfNegative(index);
            if (!unique.Add(index))
                throw new ArgumentException($"Child index '{index}' is expanded more than once.", nameof(expandedChildIndexes));
        }
        Array.Sort(indexes);

        Block = block;
        ExpandedChildIndexes = Array.AsReadOnly(indexes);
    }

    /// <summary>Gets the complete canonical serialized block.</summary>
    public MerkleBlock Block { get; }

    /// <summary>Gets the sorted child indexes expanded by other proof steps.</summary>
    public IReadOnlyList<int> ExpandedChildIndexes { get; }

    /// <inheritdoc/>
    public bool Equals(MerkleProofStep? other) =>
        ReferenceEquals(this, other)
        || (other is not null
            && Block.Equals(other.Block)
            && ExpandedChildIndexes.SequenceEqual(other.ExpandedChildIndexes));

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var hash = new HashCode();
        hash.Add(Block);
        foreach (var index in ExpandedChildIndexes)
            hash.Add(index);
        return hash.ToHashCode();
    }
}

/// <summary>Represents an immutable membership, non-membership, or range proof.</summary>
/// <remarks>
/// <see cref="Query"/> is an owned, format-defined canonical descriptor. Keeping the descriptor
/// opaque here allows a future wide-node MST format to define inclusive/exclusive range bounds
/// without coupling the transport vocabulary to a particular key codec or node representation.
/// </remarks>
public sealed record MerkleProof
{
    private readonly byte[] _query;

    /// <summary>Initializes a Merkle proof.</summary>
    /// <param name="algorithmId">The stable versioned block-format and hashing identifier.</param>
    /// <param name="domainDigest">The policy and codec domain fingerprint.</param>
    /// <param name="rootHash">The root hash against which the claim is made.</param>
    /// <param name="kind">The proof claim kind.</param>
    /// <param name="query">The complete canonical query descriptor.</param>
    /// <param name="steps">The uniquely addressed proof steps.</param>
    public MerkleProof(
        string algorithmId,
        MerkleDigest domainDigest,
        MerkleDigest rootHash,
        MerkleProofKind kind,
        ReadOnlySpan<byte> query,
        IEnumerable<MerkleProofStep> steps)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(algorithmId);
        ArgumentNullException.ThrowIfNull(steps);
        if (!Enum.IsDefined(kind))
            throw new ArgumentOutOfRangeException(nameof(kind));

        var materialized = steps.ToArray();
        var digests = new HashSet<MerkleDigest>();
        long totalByteCount = query.Length;
        foreach (var step in materialized)
        {
            ArgumentNullException.ThrowIfNull(step);
            if (!digests.Add(step.Block.Digest))
                throw new ArgumentException($"The proof contains duplicate block '{step.Block.Digest}'.", nameof(steps));
            totalByteCount = checked(totalByteCount + step.Block.Length);
        }

        AlgorithmId = algorithmId;
        DomainDigest = domainDigest;
        RootHash = rootHash;
        Kind = kind;
        _query = query.ToArray();
        Steps = Array.AsReadOnly(materialized);
        TotalByteCount = totalByteCount;
    }

    /// <summary>Gets the versioned block-format and hashing identifier.</summary>
    public string AlgorithmId { get; }

    /// <summary>Gets the policy and codec domain fingerprint.</summary>
    public MerkleDigest DomainDigest { get; }

    /// <summary>Gets the root against which the claim is made.</summary>
    public MerkleDigest RootHash { get; }

    /// <summary>Gets the proof claim kind.</summary>
    public MerkleProofKind Kind { get; }

    /// <summary>Gets the canonical format-defined query descriptor.</summary>
    public ReadOnlySpan<byte> Query => _query;

    /// <summary>Gets the uniquely addressed proof steps.</summary>
    public IReadOnlyList<MerkleProofStep> Steps { get; }

    /// <summary>Gets the combined query and serialized-block byte count.</summary>
    public long TotalByteCount { get; }

    /// <inheritdoc/>
    public bool Equals(MerkleProof? other) =>
        ReferenceEquals(this, other)
        || (other is not null
            && string.Equals(AlgorithmId, other.AlgorithmId, StringComparison.Ordinal)
            && DomainDigest == other.DomainDigest
            && RootHash == other.RootHash
            && Kind == other.Kind
            && _query.AsSpan().SequenceEqual(other._query)
            && Steps.SequenceEqual(other.Steps));

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var hash = new HashCode();
        hash.Add(AlgorithmId, StringComparer.Ordinal);
        hash.Add(DomainDigest);
        hash.Add(RootHash);
        hash.Add(Kind);
        foreach (var value in _query)
            hash.Add(value);
        foreach (var step in Steps)
            hash.Add(step);
        return hash.ToHashCode();
    }
}

/// <summary>Identifies why block loading or Merkle verification failed.</summary>
public enum MerkleVerificationFailureKind
{
    /// <summary>No failure occurred.</summary>
    None,

    /// <summary>The requested algorithm or block-format version is unsupported.</summary>
    UnsupportedAlgorithm,

    /// <summary>The policy or codec domain differs from the expected domain.</summary>
    DomainMismatch,

    /// <summary>A required block is absent from the supplied pack or store.</summary>
    MissingBlock,

    /// <summary>A block's recomputed digest differs from its claimed address.</summary>
    DigestMismatch,

    /// <summary>A serialized block is truncated or otherwise malformed.</summary>
    MalformedBlock,

    /// <summary>A decoded block is valid bytes but not the unique canonical encoding.</summary>
    NonCanonicalBlock,

    /// <summary>A pack, proof, or request repeats a digest where uniqueness is required.</summary>
    DuplicateBlock,

    /// <summary>Different serialized bytes were supplied under the same digest.</summary>
    ConflictingBlock,

    /// <summary>A child reference violates ordering, layer, or structural constraints.</summary>
    InvalidReference,

    /// <summary>The supposedly acyclic Merkle closure contains a reference cycle.</summary>
    CycleDetected,

    /// <summary>The verified blocks do not establish the proof's query claim.</summary>
    ProofMismatch,

    /// <summary>The computed root differs from the expected root.</summary>
    RootMismatch,

    /// <summary>A configured load or verification resource limit was exceeded.</summary>
    ResourceLimitExceeded,
}

/// <summary>Reports the immutable outcome of Merkle proof verification.</summary>
public sealed record MerkleProofVerificationResult
{
    private MerkleProofVerificationResult(
        bool isValid,
        MerkleVerificationFailureKind failureKind,
        string? failureMessage,
        MerkleDigest? computedRootHash,
        int verifiedBlockCount,
        long verifiedByteCount)
    {
        IsValid = isValid;
        FailureKind = failureKind;
        FailureMessage = failureMessage;
        ComputedRootHash = computedRootHash;
        VerifiedBlockCount = verifiedBlockCount;
        VerifiedByteCount = verifiedByteCount;
    }

    /// <summary>Gets whether the proof is valid.</summary>
    public bool IsValid { get; }

    /// <summary>Gets the failure classification, or <see cref="MerkleVerificationFailureKind.None"/> on success.</summary>
    public MerkleVerificationFailureKind FailureKind { get; }

    /// <summary>Gets a diagnostic failure message, or <see langword="null"/> on success.</summary>
    public string? FailureMessage { get; }

    /// <summary>Gets the root computed from the proof when one was available.</summary>
    public MerkleDigest? ComputedRootHash { get; }

    /// <summary>Gets the number of blocks whose bytes and structure were verified.</summary>
    public int VerifiedBlockCount { get; }

    /// <summary>Gets the number of serialized bytes verified.</summary>
    public long VerifiedByteCount { get; }

    /// <summary>Creates a successful proof result.</summary>
    /// <param name="computedRootHash">The root hash reconstructed from the proof.</param>
    /// <param name="verifiedBlockCount">The number of verified blocks.</param>
    /// <param name="verifiedByteCount">The number of verified serialized bytes.</param>
    /// <returns>A successful result.</returns>
    public static MerkleProofVerificationResult Success(
        MerkleDigest computedRootHash,
        int verifiedBlockCount,
        long verifiedByteCount)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(verifiedBlockCount);
        ArgumentOutOfRangeException.ThrowIfNegative(verifiedByteCount);
        return new(true, MerkleVerificationFailureKind.None, null, computedRootHash, verifiedBlockCount, verifiedByteCount);
    }

    /// <summary>Creates a failed proof result.</summary>
    /// <param name="failureKind">The non-success failure classification.</param>
    /// <param name="failureMessage">A diagnostic message.</param>
    /// <param name="verifiedBlockCount">The number of blocks verified before failure.</param>
    /// <param name="verifiedByteCount">The number of serialized bytes verified before failure.</param>
    /// <param name="computedRootHash">The partial or complete computed root hash when one was available.</param>
    /// <returns>A failed result.</returns>
    public static MerkleProofVerificationResult Failure(
        MerkleVerificationFailureKind failureKind,
        string failureMessage,
        int verifiedBlockCount,
        long verifiedByteCount,
        MerkleDigest? computedRootHash = null)
    {
        if (failureKind == MerkleVerificationFailureKind.None || !Enum.IsDefined(failureKind))
            throw new ArgumentOutOfRangeException(nameof(failureKind));
        ArgumentException.ThrowIfNullOrWhiteSpace(failureMessage);
        ArgumentOutOfRangeException.ThrowIfNegative(verifiedBlockCount);
        ArgumentOutOfRangeException.ThrowIfNegative(verifiedByteCount);
        return new(false, failureKind, failureMessage, computedRootHash, verifiedBlockCount, verifiedByteCount);
    }
}

/// <summary>Thrown when loading, storing, or verifying Merkle persistence data fails.</summary>
public sealed class MerkleVerificationException : IOException
{
    /// <summary>Initializes a Merkle verification exception.</summary>
    /// <param name="failureKind">The non-success failure classification.</param>
    /// <param name="message">The diagnostic message.</param>
    /// <param name="blockDigest">The offending or missing block digest when known.</param>
    /// <param name="innerException">The underlying decode or storage exception when present.</param>
    public MerkleVerificationException(
        MerkleVerificationFailureKind failureKind,
        string message,
        MerkleDigest? blockDigest = null,
        Exception? innerException = null)
        : base(message, innerException)
    {
        if (failureKind == MerkleVerificationFailureKind.None || !Enum.IsDefined(failureKind))
            throw new ArgumentOutOfRangeException(nameof(failureKind));
        FailureKind = failureKind;
        BlockDigest = blockDigest;
    }

    /// <summary>Gets the failure classification.</summary>
    public MerkleVerificationFailureKind FailureKind { get; }

    /// <summary>Gets the offending or missing block digest when known.</summary>
    public MerkleDigest? BlockDigest { get; }
}

/// <summary>Bounds work and memory exposure while loading or verifying untrusted Merkle data.</summary>
/// <remarks>
/// The defaults are intentionally finite. Callers processing trusted local data may provide larger
/// values explicitly, while network-facing loaders should normally retain or tighten these limits.
/// Algorithms must check cumulative limits before allocation and before following child references.
/// </remarks>
public sealed record MerkleVerificationBudget
{
    /// <summary>Gets the default bounded verification budget.</summary>
    public static MerkleVerificationBudget Default { get; } = new(
        maxBlockCount: 1_000_000,
        maxTotalByteCount: 1L << 30,
        maxBlockByteCount: 16 << 20,
        maxDepth: 256,
        maxEntryCount: 100_000_000,
        maxChildReferencesPerBlock: 65_536);

    /// <summary>Initializes a verification budget.</summary>
    /// <param name="maxBlockCount">The maximum number of distinct blocks that may be decoded.</param>
    /// <param name="maxTotalByteCount">The maximum cumulative serialized byte count.</param>
    /// <param name="maxBlockByteCount">The maximum serialized size of one block.</param>
    /// <param name="maxDepth">The maximum root-to-leaf reference depth.</param>
    /// <param name="maxEntryCount">The maximum cumulative decoded entry count.</param>
    /// <param name="maxChildReferencesPerBlock">The maximum child-reference count in one block.</param>
    public MerkleVerificationBudget(
        int maxBlockCount,
        long maxTotalByteCount,
        int maxBlockByteCount,
        int maxDepth,
        long maxEntryCount,
        int maxChildReferencesPerBlock)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(maxBlockCount);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(maxTotalByteCount);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(maxBlockByteCount);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(maxDepth);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(maxEntryCount);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(maxChildReferencesPerBlock);
        if (maxBlockByteCount > maxTotalByteCount)
            throw new ArgumentException("The per-block byte limit must not exceed the total byte limit.", nameof(maxBlockByteCount));

        MaxBlockCount = maxBlockCount;
        MaxTotalByteCount = maxTotalByteCount;
        MaxBlockByteCount = maxBlockByteCount;
        MaxDepth = maxDepth;
        MaxEntryCount = maxEntryCount;
        MaxChildReferencesPerBlock = maxChildReferencesPerBlock;
    }

    /// <summary>Gets the maximum number of distinct blocks that may be decoded.</summary>
    public int MaxBlockCount { get; }

    /// <summary>Gets the maximum cumulative serialized byte count.</summary>
    public long MaxTotalByteCount { get; }

    /// <summary>Gets the maximum serialized size of one block.</summary>
    public int MaxBlockByteCount { get; }

    /// <summary>Gets the maximum root-to-leaf reference depth.</summary>
    public int MaxDepth { get; }

    /// <summary>Gets the maximum cumulative decoded entry count.</summary>
    public long MaxEntryCount { get; }

    /// <summary>Gets the maximum child-reference count in one block.</summary>
    public int MaxChildReferencesPerBlock { get; }
}

/// <summary>Represents presence or absence of a value in one side of a three-way merge.</summary>
/// <typeparam name="TValue">The value type.</typeparam>
/// <remarks>The default value represents absence, so a present <see langword="null"/> remains distinguishable.</remarks>
public readonly record struct MerkleMergeValue<TValue>
{
    private readonly TValue? _value;

    private MerkleMergeValue(TValue value)
    {
        HasValue = true;
        _value = value;
    }

    /// <summary>Gets an absent merge value.</summary>
    public static MerkleMergeValue<TValue> Absent => default;

    /// <summary>Gets whether a value is present.</summary>
    public bool HasValue { get; }

    /// <summary>Gets the present value.</summary>
    /// <exception cref="InvalidOperationException">No value is present.</exception>
    public TValue Value => HasValue
        ? _value!
        : throw new InvalidOperationException("The merge value is absent.");

    /// <summary>Creates a present merge value.</summary>
    /// <param name="value">The value, which may itself be <see langword="null"/> for a nullable value type.</param>
    /// <returns>A present merge value.</returns>
    public static MerkleMergeValue<TValue> Present(TValue value) => new(value);

    /// <summary>Tries to retrieve the present value.</summary>
    /// <param name="value">The value when present.</param>
    /// <returns><see langword="true"/> when a value is present.</returns>
    public bool TryGetValue([MaybeNullWhen(false)] out TValue value)
    {
        value = _value;
        return HasValue;
    }

    /// <inheritdoc/>
    public override string ToString() => HasValue ? $"Present({_value})" : "Absent";
}

/// <summary>Describes a true three-way merge conflict for one key.</summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <param name="Key">The conflicting key.</param>
/// <param name="Base">The value in the common base.</param>
/// <param name="Left">The value in the left descendant.</param>
/// <param name="Right">The value in the right descendant.</param>
public sealed record MerkleThreeWayMergeConflict<TKey, TValue>(
    TKey Key,
    MerkleMergeValue<TValue> Base,
    MerkleMergeValue<TValue> Left,
    MerkleMergeValue<TValue> Right);

/// <summary>Identifies how a three-way merge conflict is resolved.</summary>
public enum MerkleMergeResolutionKind
{
    /// <summary>Leave the conflict unresolved.</summary>
    Unresolved,

    /// <summary>Keep the common-base state.</summary>
    UseBase,

    /// <summary>Keep the left-descendant state.</summary>
    UseLeft,

    /// <summary>Keep the right-descendant state.</summary>
    UseRight,

    /// <summary>Store a resolver-supplied value.</summary>
    SetValue,

    /// <summary>Remove the key.</summary>
    Delete,
}

/// <summary>Represents a typed resolution returned for one three-way merge conflict.</summary>
/// <typeparam name="TValue">The value type.</typeparam>
/// <remarks>The default value is <see cref="Unresolved"/>, preventing accidental conflict resolution.</remarks>
public readonly record struct MerkleMergeResolution<TValue>
{
    private MerkleMergeResolution(MerkleMergeResolutionKind kind, MerkleMergeValue<TValue> value)
    {
        Kind = kind;
        Value = value;
    }

    /// <summary>Gets a resolution that preserves the common-base state.</summary>
    public static MerkleMergeResolution<TValue> UseBase { get; } = new(MerkleMergeResolutionKind.UseBase, default);

    /// <summary>Gets a resolution that preserves the left-descendant state.</summary>
    public static MerkleMergeResolution<TValue> UseLeft { get; } = new(MerkleMergeResolutionKind.UseLeft, default);

    /// <summary>Gets a resolution that preserves the right-descendant state.</summary>
    public static MerkleMergeResolution<TValue> UseRight { get; } = new(MerkleMergeResolutionKind.UseRight, default);

    /// <summary>Gets a resolution that removes the key.</summary>
    public static MerkleMergeResolution<TValue> Delete { get; } = new(MerkleMergeResolutionKind.Delete, default);

    /// <summary>Gets a resolution that leaves the conflict unresolved.</summary>
    public static MerkleMergeResolution<TValue> Unresolved { get; } = new(MerkleMergeResolutionKind.Unresolved, default);

    /// <summary>Gets the resolution kind.</summary>
    public MerkleMergeResolutionKind Kind { get; }

    /// <summary>Gets the custom value for <see cref="MerkleMergeResolutionKind.SetValue"/>.</summary>
    /// <remarks>For every other resolution kind this value is absent.</remarks>
    public MerkleMergeValue<TValue> Value { get; }

    /// <summary>Creates a resolution that stores a resolver-supplied value.</summary>
    /// <param name="value">The resolved value.</param>
    /// <returns>A custom-value resolution.</returns>
    public static MerkleMergeResolution<TValue> SetValue(TValue value) =>
        new(MerkleMergeResolutionKind.SetValue, MerkleMergeValue<TValue>.Present(value));
}

/// <summary>Resolves a true key-level conflict during a three-way Merkle merge.</summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <param name="conflict">The base, left, and right states for the conflicting key.</param>
/// <returns>The requested resolution.</returns>
/// <remarks>
/// Merge algorithms should invoke this resolver only when both descendants changed relative to the
/// base and the descendant states are not semantically equal. Non-conflicting edits are combined
/// without a callback.
/// </remarks>
public delegate MerkleMergeResolution<TValue> MerkleThreeWayMergeResolver<TKey, TValue>(
    MerkleThreeWayMergeConflict<TKey, TValue> conflict);

/// <summary>Represents the immutable outcome of a typed three-way Merkle merge.</summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <remarks>
/// A successful result contains a complete merged tree and no unresolved conflicts. A conflicted
/// result contains all unresolved conflicts and intentionally exposes no partial tree, avoiding
/// accidental publication of a state whose conflict policy is incomplete.
/// </remarks>
public sealed record MerkleThreeWayMergeResult<TKey, TValue>
{
    private MerkleThreeWayMergeResult(
        MerkleSearchTree<TKey, TValue>? mergedTree,
        IReadOnlyList<MerkleThreeWayMergeConflict<TKey, TValue>> unresolvedConflicts)
    {
        MergedTree = mergedTree;
        UnresolvedConflicts = unresolvedConflicts;
    }

    /// <summary>Gets the complete merged tree on success, or <see langword="null"/> when conflicts remain.</summary>
    public MerkleSearchTree<TKey, TValue>? MergedTree { get; }

    /// <summary>Gets the unresolved key-level conflicts.</summary>
    public IReadOnlyList<MerkleThreeWayMergeConflict<TKey, TValue>> UnresolvedConflicts { get; }

    /// <summary>Gets whether the merge produced a complete publishable tree.</summary>
    [MemberNotNullWhen(true, nameof(MergedTree))]
    public bool IsSuccess => MergedTree is not null;

    /// <summary>Creates a successful merge result.</summary>
    /// <param name="mergedTree">The complete merged tree.</param>
    /// <returns>A successful result.</returns>
    public static MerkleThreeWayMergeResult<TKey, TValue> Success(MerkleSearchTree<TKey, TValue> mergedTree)
    {
        ArgumentNullException.ThrowIfNull(mergedTree);
        return new(mergedTree, Array.Empty<MerkleThreeWayMergeConflict<TKey, TValue>>());
    }

    /// <summary>Creates a conflicted merge result without a partial tree.</summary>
    /// <param name="unresolvedConflicts">The non-empty set of unresolved conflicts.</param>
    /// <returns>A conflicted result.</returns>
    public static MerkleThreeWayMergeResult<TKey, TValue> Conflicted(
        IEnumerable<MerkleThreeWayMergeConflict<TKey, TValue>> unresolvedConflicts)
    {
        ArgumentNullException.ThrowIfNull(unresolvedConflicts);
        var materialized = unresolvedConflicts.ToArray();
        if (materialized.Length == 0)
            throw new ArgumentException("A conflicted merge result must contain at least one conflict.", nameof(unresolvedConflicts));
        foreach (var conflict in materialized)
            ArgumentNullException.ThrowIfNull(conflict);
        return new(null, new ReadOnlyCollection<MerkleThreeWayMergeConflict<TKey, TValue>>(materialized));
    }

    /// <inheritdoc/>
    public bool Equals(MerkleThreeWayMergeResult<TKey, TValue>? other) =>
        ReferenceEquals(this, other)
        || (other is not null
            && ReferenceEquals(MergedTree, other.MergedTree)
            && UnresolvedConflicts.SequenceEqual(other.UnresolvedConflicts));

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var hash = new HashCode();
        hash.Add(MergedTree);
        foreach (var conflict in UnresolvedConflicts)
            hash.Add(conflict);
        return hash.ToHashCode();
    }
}
