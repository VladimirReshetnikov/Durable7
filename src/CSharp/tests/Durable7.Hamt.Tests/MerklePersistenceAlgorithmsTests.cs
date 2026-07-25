using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Adversarial persistence, synchronization, proof, and merge coverage for the wide-node Merkle tree.</summary>
public sealed class MerklePersistenceAlgorithmsTests
{
    /// <summary>Locks the v2 domain, empty-root, block, and root bytes to a cross-process golden vector.</summary>
    [Fact]
    public void CanonicalBlockWireFormat_MatchesGoldenVector()
    {
        var policy = IntStringPolicy("golden-int-string-v1");
        var tree = MerkleSearchTree<int, string?>.Create(policy).SetItem(42, "forty-two");
        var block = Assert.Single(tree.ExportPack().Blocks);

        Assert.Equal("mst-sha256-b16-v2", MerkleSearchTreePolicy<int, string?>.AlgorithmId);
        Assert.Equal("fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2", policy.DomainDigest.ToString());
        Assert.Equal("1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94", tree.RootHash.ToString());
        Assert.Equal(
            "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2000000000100000001000000040000002a0000000a01666f7274792d74776f98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb398900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3",
            Convert.ToHexString(block.Content).ToLowerInvariant());
    }

    /// <summary>Round-trips exact block bytes through export, save, load, and import.</summary>
    [Fact]
    public void SaveLoadExportImport_RoundTripExactClosure()
    {
        var policy = IntStringPolicy();
        var tree = CreateTree(policy, 513);
        var exported = tree.ExportPack();

        Assert.True(tree.BlockCount > 2);
        Assert.Equal(tree.BlockCount, exported.BlockCount);
        Assert.Equal(tree.RootHash, exported.RootHash);
        Assert.Equal(policy.DomainDigest, exported.DomainDigest);
        Assert.True(exported.ContainsRootBlock);
        Assert.Equal(exported, tree.ExportPack());

        var store = new InMemoryMerkleBlockStore();
        Assert.Equal(tree.BlockCount, tree.Save(store));
        Assert.Equal(0, tree.Save(store));
        Assert.Equal(tree.BlockCount, store.Count);

        var loaded = MerkleSearchTree<int, string?>.Load(tree.RootHash, policy, store);
        AssertTreesEqual(tree, loaded);
        Assert.Equal(exported, loaded.ExportPack());

        var importedStore = new InMemoryMerkleBlockStore();
        var imported = MerkleSearchTree<int, string?>.Import(exported, policy, importedStore);
        AssertTreesEqual(tree, imported);
        Assert.Equal(tree.BlockCount, importedStore.Count);
        Assert.Equal(exported, imported.ExportPack());

        var empty = MerkleSearchTree<int, string?>.Create(policy);
        var emptyPack = empty.ExportPack();
        Assert.Empty(emptyPack.Blocks);
        AssertTreesEqual(empty, MerkleSearchTree<int, string?>.Import(emptyPack, policy));
        AssertTreesEqual(empty, MerkleSearchTree<int, string?>.Load(empty.RootHash, policy, new InMemoryMerkleBlockStore()));
    }

    /// <summary>Rejects absent closure blocks and digest/content tampering during load and import.</summary>
    [Fact]
    public void LoadAndImport_RejectMissingAndTamperedBlocks()
    {
        var policy = IntStringPolicy();
        var tree = CreateTree(policy, 257);
        var pack = tree.ExportPack();
        Assert.True(pack.BlockCount > 1);

        var missingBlock = pack.Blocks[^1];
        var incomplete = new MerkleBlockPack(
            pack.AlgorithmId,
            pack.DomainDigest,
            pack.RootHash,
            pack.Blocks.Where(block => block.Digest != missingBlock.Digest));
        var missing = AssertVerification(
            MerkleVerificationFailureKind.MissingBlock,
            () => MerkleSearchTree<int, string?>.Import(incomplete, policy));
        Assert.Equal(missingBlock.Digest, missing.BlockDigest);

        var store = new InMemoryMerkleBlockStore();
        _ = tree.Save(store);
        Assert.True(store.Remove(missingBlock.Digest));
        AssertVerification(
            MerkleVerificationFailureKind.MissingBlock,
            () => MerkleSearchTree<int, string?>.Load(tree.RootHash, policy, store));

        var root = pack.Blocks.Single(block => block.Digest == pack.RootHash);
        var tamperedBytes = root.ToArray();
        tamperedBytes[^1] ^= 0x80;
        var tamperedRoot = new MerkleBlock(root.Digest, tamperedBytes);
        var tamperedPack = ReplaceBlock(pack, tamperedRoot);
        AssertVerification(
            MerkleVerificationFailureKind.DigestMismatch,
            () => MerkleSearchTree<int, string?>.Import(tamperedPack, policy));

        var tamperedStore = new InMemoryMerkleBlockStore();
        foreach (var block in pack.Blocks)
            _ = tamperedStore.Put(block.Digest == root.Digest ? tamperedRoot : block);
        AssertVerification(
            MerkleVerificationFailureKind.DigestMismatch,
            () => MerkleSearchTree<int, string?>.Load(tree.RootHash, policy, tamperedStore));
    }

    /// <summary>Rejects validly addressed but malformed/noncanonical bytes, foreign domains, and exhausted budgets.</summary>
    [Fact]
    public void Import_RejectsMalformedNoncanonicalDomainAndBudgetViolations()
    {
        var policy = IntStringPolicy();
        var one = MerkleSearchTree<int, string?>.Create(policy).SetItem(1, "one");
        var oneBlock = Assert.Single(one.ExportPack().Blocks);

        var noncanonicalBytes = oneBlock.ToArray().Append((byte)0).ToArray();
        var noncanonical = AddressedBlock(noncanonicalBytes);
        var noncanonicalPack = new MerkleBlockPack(
            MerkleSearchTreePolicy<int, string?>.AlgorithmId,
            policy.DomainDigest,
            noncanonical.Digest,
            [noncanonical]);
        AssertVerification(
            MerkleVerificationFailureKind.NonCanonicalBlock,
            () => MerkleSearchTree<int, string?>.Import(noncanonicalPack, policy));

        var malformedBytes = oneBlock.ToArray();
        malformedBytes[0] ^= 0xff;
        var malformed = AddressedBlock(malformedBytes);
        var malformedPack = new MerkleBlockPack(
            MerkleSearchTreePolicy<int, string?>.AlgorithmId,
            policy.DomainDigest,
            malformed.Digest,
            [malformed]);
        AssertVerification(
            MerkleVerificationFailureKind.MalformedBlock,
            () => MerkleSearchTree<int, string?>.Import(malformedPack, policy));

        var foreignPolicy = IntStringPolicy("foreign-wire-domain-v1");
        var foreignTree = MerkleSearchTree<int, string?>.Create(foreignPolicy).SetItem(1, "one");
        var relabeledForeignPack = new MerkleBlockPack(
            MerkleSearchTreePolicy<int, string?>.AlgorithmId,
            policy.DomainDigest,
            foreignTree.RootHash,
            foreignTree.ExportPack().Blocks);
        AssertVerification(
            MerkleVerificationFailureKind.DomainMismatch,
            () => MerkleSearchTree<int, string?>.Import(relabeledForeignPack, policy));

        var many = CreateTree(policy, 257);
        Assert.True(many.BlockCount > 1);
        var oneBlockBudget = new MerkleVerificationBudget(
            maxBlockCount: 1,
            maxTotalByteCount: 1L << 30,
            maxBlockByteCount: 16 << 20,
            maxDepth: 256,
            maxEntryCount: 1_000_000,
            maxChildReferencesPerBlock: 65_536);
        Assert.Equal(oneBlockBudget.MaxBlockByteCount, oneBlockBudget.MaxProofQueryByteCount);
        AssertVerification(
            MerkleVerificationFailureKind.ResourceLimitExceeded,
            () => MerkleSearchTree<int, string?>.Import(many.ExportPack(), policy, budget: oneBlockBudget));
    }

    /// <summary>Preflights late store conflicts and commits neither save nor import blocks on failure.</summary>
    [Fact]
    public void SaveAndImport_AreAtomicAgainstDestinationConflicts()
    {
        var policy = IntStringPolicy();
        var tree = CreateTree(policy, 257);
        var pack = tree.ExportPack();
        Assert.True(pack.BlockCount > 2);
        var lateBlock = pack.Blocks[^1];
        var conflicting = new MerkleBlock(lateBlock.Digest, [0xde, 0xad, 0xbe, 0xef]);

        var saveStore = new RecordingBlockStore();
        saveStore.SeedUnsafe(conflicting);
        AssertVerification(MerkleVerificationFailureKind.ConflictingBlock, () => tree.Save(saveStore));
        Assert.Equal(0, saveStore.PutCallCount);
        Assert.Equal(1, saveStore.Count);
        Assert.True(saveStore.TryGet(lateBlock.Digest, out var savedConflict));
        Assert.Equal(conflicting, savedConflict);

        var importStore = new RecordingBlockStore();
        importStore.SeedUnsafe(conflicting);
        AssertVerification(
            MerkleVerificationFailureKind.ConflictingBlock,
            () => MerkleSearchTree<int, string?>.Import(pack, policy, importStore));
        Assert.Equal(0, importStore.PutCallCount);
        Assert.Equal(1, importStore.Count);
        Assert.True(importStore.TryGet(lateBlock.Digest, out var importedConflict));
        Assert.Equal(conflicting, importedConflict);

        var sentinel = AddressedBlock([0x73, 0x65, 0x6e, 0x74, 0x69, 0x6e, 0x65, 0x6c]);
        var failureStore = new RecordingBlockStore();
        failureStore.SeedUnsafe(sentinel);
        var root = pack.Blocks.Single(block => block.Digest == pack.RootHash);
        var brokenBytes = root.ToArray();
        brokenBytes[^1] ^= 1;
        var brokenPack = ReplaceBlock(pack, new MerkleBlock(root.Digest, brokenBytes));
        AssertVerification(
            MerkleVerificationFailureKind.DigestMismatch,
            () => MerkleSearchTree<int, string?>.Import(brokenPack, policy, failureStore));
        Assert.Equal(0, failureStore.PutCallCount);
        Assert.Equal(1, failureStore.Count);
        Assert.True(failureStore.TryGet(sentinel.Digest, out var retained));
        Assert.Equal(sentinel, retained);
    }

    /// <summary>Completes an empty-store transfer and a receiver-assisted partial frontier transfer.</summary>
    [Fact]
    public void SyncPlansAndPacks_CoverCompleteAndPartialClosures()
    {
        var policy = IntStringPolicy();
        var target = CreateTree(policy, 513);
        var local = MerkleSearchTree<int, string?>.Create(policy);

        var emptyReceiver = new InMemoryMerkleBlockStore();
        var completePack = target.CreateSyncPack(emptyReceiver);
        Assert.Equal(target.ExportPack(), completePack);
        var complete = MerkleSearchTree<int, string?>.Import(completePack, policy, emptyReceiver);
        AssertTreesEqual(target, complete);
        Assert.Equal(target.BlockCount, emptyReceiver.Count);

        var partialReceiver = new InMemoryMerkleBlockStore();
        _ = target.Save(partialReceiver);
        var missing = target.ExportPack().Blocks[^1];
        Assert.NotEqual(target.RootHash, missing.Digest);
        Assert.True(partialReceiver.Remove(missing.Digest));

        var plan = target.PlanSync(local, partialReceiver);
        Assert.False(plan.RootsMatch);
        Assert.True(plan.RequiresBlocks);
        Assert.Equal([missing.Digest], plan.RequestedBlocks);
        Assert.True(plan.ExaminedBlockCount > 0);
        Assert.True(plan.ExaminedByteCount > 0);

        var partialPack = target.ExportPack(plan.RequestedBlocks);
        Assert.False(partialPack.ContainsRootBlock);
        Assert.Equal([missing], partialPack.Blocks);
        var repaired = MerkleSearchTree<int, string?>.Import(partialPack, policy, partialReceiver);
        AssertTreesEqual(target, repaired);
        Assert.Equal(target.BlockCount, partialReceiver.Count);

        var converged = target.PlanSync(repaired, partialReceiver);
        Assert.True(converged.RootsMatch);
        Assert.False(converged.RequiresBlocks);
        Assert.Empty(converged.RequestedBlocks);
        Assert.Equal(0, converged.ExaminedBlockCount);
        Assert.Equal(0, converged.ExaminedByteCount);
    }

    /// <summary>Verifies canonical membership, non-membership, and inclusive-range proofs.</summary>
    [Fact]
    public void Proofs_VerifyMembershipNonMembershipAndRangeClaims()
    {
        var policy = IntStringPolicy();
        var tree = CreateTree(policy, 513);

        var membership = tree.CreateProof(0);
        Assert.Equal(MerkleProofKind.Membership, membership.Kind);
        AssertProofValid(membership, policy);

        var nonMembership = tree.CreateProof(10_000);
        Assert.Equal(MerkleProofKind.NonMembership, nonMembership.Kind);
        AssertProofValid(nonMembership, policy);

        var range = tree.CreateRangeProof(-20, 20);
        Assert.Equal(MerkleProofKind.Range, range.Kind);
        AssertProofValid(range, policy);

        var empty = MerkleSearchTree<int, string?>.Create(policy);
        AssertProofValid(empty.CreateProof(1), policy);
        AssertProofValid(empty.CreateRangeProof(-1, 1), policy);
    }

    /// <summary>Rejects an oversized proof query before decoding codecs or supplied blocks.</summary>
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ProofVerification_RejectsOversizedQueryBeforeDecode(bool emptyTree)
    {
        var keyCodec = new CountingCodec<int>(MerkleCodecs.Int32);
        var valueCodec = new CountingCodec<string?>(MerkleCodecs.Utf8String);
        var policy = MerkleSearchTreePolicy<int, string?>.Create(
            "proof-query-budget-v1",
            Comparer<int>.Default,
            keyCodec,
            valueCodec);
        var tree = MerkleSearchTree<int, string?>.Create(policy);
        if (!emptyTree)
            tree = tree.SetItem(1, "one");
        var proof = tree.CreateProof(1);
        Assert.False(proof.Query.IsEmpty);
        if (!emptyTree)
            Assert.NotEmpty(proof.Steps);

        keyCodec.Reset();
        valueCodec.Reset();
        var budget = new MerkleVerificationBudget(
            maxBlockCount: 1_000,
            maxTotalByteCount: 1 << 20,
            maxBlockByteCount: 1 << 20,
            maxDepth: 256,
            maxEntryCount: 1_000_000,
            maxChildReferencesPerBlock: 65_536,
            maxProofQueryByteCount: proof.Query.Length - 1);

        var result = MerkleSearchTree<int, string?>.VerifyProof(proof, policy, budget);

        Assert.False(result.IsValid);
        Assert.Equal(MerkleVerificationFailureKind.ResourceLimitExceeded, result.FailureKind);
        Assert.Equal(0, result.VerifiedBlockCount);
        Assert.Equal(0, result.VerifiedByteCount);
        Assert.Equal(0, keyCodec.EncodeCallCount);
        Assert.Equal(0, keyCodec.DecodeCallCount);
        Assert.Equal(0, valueCodec.EncodeCallCount);
        Assert.Equal(0, valueCodec.DecodeCallCount);
    }

    /// <summary>Rejects excessive proof steps and expansions before verifier-side allocation or decoding.</summary>
    [Fact]
    public void ProofVerification_RejectsOversizedStructureBeforeDecode()
    {
        var keyCodec = new CountingCodec<int>(MerkleCodecs.Int32);
        var valueCodec = new CountingCodec<string?>(MerkleCodecs.Utf8String);
        var policy = MerkleSearchTreePolicy<int, string?>.Create(
            "proof-structure-budget-v1",
            Comparer<int>.Default,
            keyCodec,
            valueCodec);
        var proof = CreateTree(policy, 513).CreateProof(0);
        Assert.True(proof.Steps.Count > 1);

        keyCodec.Reset();
        valueCodec.Reset();
        var stepBudget = new MerkleVerificationBudget(
            maxBlockCount: proof.Steps.Count - 1,
            maxTotalByteCount: 1L << 30,
            maxBlockByteCount: 16 << 20,
            maxDepth: 256,
            maxEntryCount: 100_000_000,
            maxChildReferencesPerBlock: 65_536,
            maxProofQueryByteCount: 16 << 20);

        var stepResult = MerkleSearchTree<int, string?>.VerifyProof(proof, policy, stepBudget);

        AssertEarlyProofLimitFailure(stepResult, proof.Query.Length, keyCodec, valueCodec);

        var expandedSteps = proof.Steps.ToArray();
        expandedSteps[0] = new MerkleProofStep(expandedSteps[0].Block, [0, 1]);
        var expandedProof = RebuildProof(proof, proof.Query, expandedSteps);
        keyCodec.Reset();
        valueCodec.Reset();
        var expansionBudget = new MerkleVerificationBudget(
            maxBlockCount: 1_000,
            maxTotalByteCount: 1L << 30,
            maxBlockByteCount: 16 << 20,
            maxDepth: 256,
            maxEntryCount: 100_000_000,
            maxChildReferencesPerBlock: 1,
            maxProofQueryByteCount: 16 << 20);

        var expansionResult = MerkleSearchTree<int, string?>.VerifyProof(expandedProof, policy, expansionBudget);

        AssertEarlyProofLimitFailure(expansionResult, expandedProof.Query.Length, keyCodec, valueCodec);
    }

    /// <summary>Rejects changed block bytes, changed queries, and authenticated but noncanonical extra steps.</summary>
    [Fact]
    public void ProofVerification_RejectsTamperedQueryAndExtraSteps()
    {
        var policy = IntStringPolicy();
        var tree = CreateTree(policy, 513);
        var proof = tree.CreateProof(0);
        Assert.NotEmpty(proof.Steps);

        var tamperedSteps = proof.Steps.ToArray();
        var first = tamperedSteps[0];
        var changedBytes = first.Block.ToArray();
        changedBytes[^1] ^= 0x40;
        tamperedSteps[0] = new MerkleProofStep(
            new MerkleBlock(first.Block.Digest, changedBytes),
            first.ExpandedChildIndexes);
        var tamperedProof = RebuildProof(proof, proof.Query, tamperedSteps);
        AssertProofFailure(tamperedProof, policy, MerkleVerificationFailureKind.DigestMismatch);

        var trailingQuery = proof.Query.ToArray().Append((byte)0).ToArray();
        var changedQueryProof = RebuildProof(proof, trailingQuery, proof.Steps);
        AssertProofFailure(changedQueryProof, policy, MerkleVerificationFailureKind.ProofMismatch);

        var changedValueQuery = proof.Query.ToArray();
        changedValueQuery[^1] ^= 1;
        var changedValueProof = RebuildProof(proof, changedValueQuery, proof.Steps);
        AssertProofFailure(changedValueProof, policy, MerkleVerificationFailureKind.ProofMismatch);

        var proofDigests = proof.Steps.Select(step => step.Block.Digest).ToHashSet();
        var extra = tree.ExportPack().Blocks.First(block => !proofDigests.Contains(block.Digest));
        var extraSteps = proof.Steps.Append(new MerkleProofStep(extra, [])).ToArray();
        var extraProof = RebuildProof(proof, proof.Query, extraSteps);
        AssertProofFailure(extraProof, policy, MerkleVerificationFailureKind.ProofMismatch);
    }

    /// <summary>Combines disjoint edits and collapses identical edits without invoking a resolver.</summary>
    [Fact]
    public void ThreeWayMerge_CombinesDisjointAndSameEdits()
    {
        var policy = IntStringPolicy();
        var @base = MerkleSearchTree<int, string?>.Create(policy)
            .SetItem(1, "one")
            .SetItem(2, "two")
            .SetItem(3, "three");

        var left = @base.SetItem(1, "ONE");
        var right = @base.SetItem(2, "TWO");
        var disjoint = MerkleSearchTree<int, string?>.Merge(@base, left, right, _ =>
            throw new InvalidOperationException("The resolver must not run for disjoint edits."));
        var disjointTree = AssertSuccess(disjoint);
        Assert.Equal("ONE", disjointTree[1]);
        Assert.Equal("TWO", disjointTree[2]);
        Assert.Equal("three", disjointTree[3]);

        var sameLeft = @base.SetItem(3, "THREE");
        var sameRight = @base.SetItem(3, "THREE");
        var same = MerkleSearchTree<int, string?>.Merge(@base, sameLeft, sameRight, _ =>
            throw new InvalidOperationException("The resolver must not run for identical edits."));
        Assert.Equal("THREE", AssertSuccess(same)[3]);
    }

    /// <summary>Reports typed conflicts and applies explicit custom, side, and delete resolutions.</summary>
    [Fact]
    public void ThreeWayMerge_ReportsAndResolvesConflicts()
    {
        var policy = IntStringPolicy();
        var @base = MerkleSearchTree<int, string?>.Create(policy).SetItem(1, "base");
        var left = @base.SetItem(1, "left");
        var right = @base.SetItem(1, "right");

        var unresolved = MerkleSearchTree<int, string?>.Merge(@base, left, right);
        Assert.False(unresolved.IsSuccess);
        Assert.Null(unresolved.MergedTree);
        var conflict = Assert.Single(unresolved.UnresolvedConflicts);
        Assert.Equal(1, conflict.Key);
        AssertMergeValue(conflict.Base, "base");
        AssertMergeValue(conflict.Left, "left");
        AssertMergeValue(conflict.Right, "right");

        var resolved = MerkleSearchTree<int, string?>.Merge(
            @base,
            left,
            right,
            observed =>
            {
                Assert.Equal(conflict, observed);
                return MerkleMergeResolution<string?>.SetValue("resolved");
            });
        Assert.Equal("resolved", AssertSuccess(resolved)[1]);

        var useRight = MerkleSearchTree<int, string?>.Merge(
            @base, left, right, _ => MerkleMergeResolution<string?>.UseRight);
        Assert.Equal("right", AssertSuccess(useRight)[1]);

        var deleted = MerkleSearchTree<int, string?>.Merge(
            @base, left, right, _ => MerkleMergeResolution<string?>.Delete);
        Assert.False(AssertSuccess(deleted).ContainsKey(1));
    }

    /// <summary>Distinguishes a present null value from deletion in conflicts and resolver output.</summary>
    [Fact]
    public void ThreeWayMerge_PreservesPresentNullVersusDelete()
    {
        var policy = IntStringPolicy();
        var @base = MerkleSearchTree<int, string?>.Create(policy).SetItem(1, "base");
        var presentNull = @base.SetItem(1, null);
        var deleted = @base.Remove(1);

        var unresolved = MerkleSearchTree<int, string?>.Merge(@base, presentNull, deleted);
        var conflict = Assert.Single(unresolved.UnresolvedConflicts);
        Assert.True(conflict.Left.HasValue);
        Assert.Null(conflict.Left.Value);
        Assert.False(conflict.Right.HasValue);
        Assert.Throws<InvalidOperationException>(() => _ = conflict.Right.Value);

        var keepNull = MerkleSearchTree<int, string?>.Merge(
            @base,
            presentNull,
            deleted,
            _ => MerkleMergeResolution<string?>.SetValue(null));
        var nullTree = AssertSuccess(keepNull);
        Assert.True(nullTree.TryGetValue(1, out var nullValue));
        Assert.Null(nullValue);

        var remove = MerkleSearchTree<int, string?>.Merge(
            @base,
            presentNull,
            deleted,
            _ => MerkleMergeResolution<string?>.Delete);
        Assert.False(AssertSuccess(remove).ContainsKey(1));
    }

    private static MerkleSearchTreePolicy<int, string?> IntStringPolicy(
        string policyId = "persistence-algorithms-test-v1") =>
        MerkleSearchTreePolicy<int, string?>.Create(
            policyId,
            Comparer<int>.Default,
            MerkleCodecs.Int32,
            MerkleCodecs.Utf8String);

    private static MerkleSearchTree<int, string?> CreateTree(
        MerkleSearchTreePolicy<int, string?> policy,
        int count)
    {
        var first = -(count / 2);
        return MerkleSearchTree<int, string?>.CreateRange(
            Enumerable.Range(first, count).Select(key =>
                KeyValuePair.Create<int, string?>(key, key % 29 == 0 ? null : $"value:{key}")),
            policy);
    }

    private static MerkleBlockPack ReplaceBlock(MerkleBlockPack pack, MerkleBlock replacement) =>
        new(
            pack.AlgorithmId,
            pack.DomainDigest,
            pack.RootHash,
            pack.Blocks.Select(block => block.Digest == replacement.Digest ? replacement : block));

    private static MerkleBlock AddressedBlock(ReadOnlySpan<byte> content) =>
        new(MerkleDigest.FromBytes(SHA256.HashData(content)), content);

    private static MerkleVerificationException AssertVerification(
        MerkleVerificationFailureKind expected,
        Action operation)
    {
        var exception = Assert.Throws<MerkleVerificationException>(operation);
        Assert.Equal(expected, exception.FailureKind);
        return exception;
    }

    private static void AssertTreesEqual<TKey, TValue>(
        MerkleSearchTree<TKey, TValue> expected,
        MerkleSearchTree<TKey, TValue> actual)
    {
        Assert.Equal(expected.RootHash, actual.RootHash);
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected.BlockCount, actual.BlockCount);
        Assert.Equal(expected.ToArray(), actual.ToArray());
        Assert.Equal(expected.ValidateStructure(), actual.ValidateStructure());
    }

    private static void AssertProofValid<TKey, TValue>(
        MerkleProof proof,
        MerkleSearchTreePolicy<TKey, TValue> policy)
    {
        var result = MerkleSearchTree<TKey, TValue>.VerifyProof(proof, policy);
        Assert.True(result.IsValid, result.FailureMessage);
        Assert.Equal(MerkleVerificationFailureKind.None, result.FailureKind);
        Assert.Equal(proof.RootHash, result.ComputedRootHash);
        Assert.Equal(proof.Steps.Count, result.VerifiedBlockCount);
        Assert.Equal(proof.TotalByteCount, result.VerifiedByteCount);
    }

    private static void AssertProofFailure<TKey, TValue>(
        MerkleProof proof,
        MerkleSearchTreePolicy<TKey, TValue> policy,
        MerkleVerificationFailureKind expected)
    {
        var result = MerkleSearchTree<TKey, TValue>.VerifyProof(proof, policy);
        Assert.False(result.IsValid);
        Assert.Equal(expected, result.FailureKind);
        Assert.False(string.IsNullOrWhiteSpace(result.FailureMessage));
    }

    private static void AssertEarlyProofLimitFailure(
        MerkleProofVerificationResult result,
        int expectedQueryByteCount,
        CountingCodec<int> keyCodec,
        CountingCodec<string?> valueCodec)
    {
        Assert.False(result.IsValid);
        Assert.Equal(MerkleVerificationFailureKind.ResourceLimitExceeded, result.FailureKind);
        Assert.Equal(0, result.VerifiedBlockCount);
        Assert.Equal(expectedQueryByteCount, result.VerifiedByteCount);
        Assert.Equal(0, keyCodec.EncodeCallCount);
        Assert.Equal(0, keyCodec.DecodeCallCount);
        Assert.Equal(0, valueCodec.EncodeCallCount);
        Assert.Equal(0, valueCodec.DecodeCallCount);
    }

    private static MerkleProof RebuildProof(
        MerkleProof source,
        ReadOnlySpan<byte> query,
        IEnumerable<MerkleProofStep> steps) =>
        new(
            source.AlgorithmId,
            source.DomainDigest,
            source.RootHash,
            source.Kind,
            query,
            steps);

    private static MerkleSearchTree<TKey, TValue> AssertSuccess<TKey, TValue>(
        MerkleThreeWayMergeResult<TKey, TValue> result)
    {
        Assert.True(result.IsSuccess);
        Assert.Empty(result.UnresolvedConflicts);
        return Assert.IsType<MerkleSearchTree<TKey, TValue>>(result.MergedTree);
    }

    private static void AssertMergeValue<T>(MerkleMergeValue<T> actual, T expected)
    {
        Assert.True(actual.HasValue);
        Assert.Equal(expected, actual.Value);
    }

    private sealed class RecordingBlockStore : IMerkleBlockStore
    {
        private readonly Dictionary<MerkleDigest, MerkleBlock> _blocks = [];

        public int Count => _blocks.Count;

        public IReadOnlyCollection<MerkleDigest> Digests => Array.AsReadOnly(_blocks.Keys.Order().ToArray());

        public int PutCallCount { get; private set; }

        public bool Contains(MerkleDigest digest) => _blocks.ContainsKey(digest);

        public bool TryGet(MerkleDigest digest, [NotNullWhen(true)] out MerkleBlock? block) =>
            _blocks.TryGetValue(digest, out block);

        public bool Put(MerkleBlock block)
        {
            PutCallCount++;
            if (_blocks.TryGetValue(block.Digest, out var existing))
            {
                if (existing.Equals(block))
                    return false;
                throw new MerkleVerificationException(
                    MerkleVerificationFailureKind.ConflictingBlock,
                    $"Digest '{block.Digest}' is already associated with different block bytes.",
                    block.Digest);
            }
            _blocks.Add(block.Digest, block);
            return true;
        }

        public bool Remove(MerkleDigest digest) => _blocks.Remove(digest);

        public void Clear() => _blocks.Clear();

        internal void SeedUnsafe(MerkleBlock block) => _blocks.Add(block.Digest, block);
    }

    private sealed class CountingCodec<T>(IMerkleCodec<T> inner) : IMerkleCodec<T>
    {
        public string EncodingId => inner.EncodingId;

        internal int EncodeCallCount { get; private set; }

        internal int DecodeCallCount { get; private set; }

        public byte[] Encode(T value)
        {
            EncodeCallCount++;
            return inner.Encode(value);
        }

        public T Decode(ReadOnlySpan<byte> encoding)
        {
            DecodeCallCount++;
            return inner.Decode(encoding);
        }

        internal void Reset()
        {
            EncodeCallCount = 0;
            DecodeCallCount = 0;
        }
    }
}
