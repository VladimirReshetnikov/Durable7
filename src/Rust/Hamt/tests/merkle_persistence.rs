use std::collections::HashMap;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread;

use tools_data_structures_hamt::{
    InMemoryMerkleBlockStore, Int32MerkleCodec, MerkleBlock, MerkleBlockPack, MerkleBlockStore,
    MerkleCodec, MerkleCodecError, MerkleDigest, MerkleMergeResolution, MerkleProof,
    MerkleProofKind, MerkleProofStep, MerkleSearchTree, MerkleSearchTreePolicy,
    MerkleThreeWayMergeConflict, MerkleVerificationBudget, MerkleVerificationError,
    MerkleVerificationFailureKind, NullableUtf8MerkleCodec,
};

type Tree = MerkleSearchTree<i32, Option<String>>;

fn policy_with_id(id: &str) -> MerkleSearchTreePolicy<i32, Option<String>> {
    MerkleSearchTreePolicy::natural(id, Int32MerkleCodec, NullableUtf8MerkleCodec).unwrap()
}

fn policy() -> MerkleSearchTreePolicy<i32, Option<String>> {
    policy_with_id("persistence-algorithms-test-v1")
}

fn create_tree(policy: &MerkleSearchTreePolicy<i32, Option<String>>, count: i32) -> Tree {
    let first = -(count / 2);
    Tree::from_entries(
        (first..first + count).map(|key| (key, (key % 29 != 0).then(|| format!("value:{key}")))),
        policy.clone(),
    )
    .unwrap()
}

fn assert_trees_equal(expected: &Tree, actual: &Tree) {
    assert_eq!(expected.root_hash(), actual.root_hash());
    assert_eq!(expected.len(), actual.len());
    assert_eq!(expected.block_count(), actual.block_count());
    assert_eq!(
        expected
            .iter()
            .map(|entry| (*entry.key(), entry.value().clone()))
            .collect::<Vec<_>>(),
        actual
            .iter()
            .map(|entry| (*entry.key(), entry.value().clone()))
            .collect::<Vec<_>>()
    );
    assert_eq!(
        expected.validate_structure().unwrap(),
        actual.validate_structure().unwrap()
    );
}

fn assert_failure<T>(
    expected: MerkleVerificationFailureKind,
    result: Result<T, MerkleVerificationError>,
) -> MerkleVerificationError {
    let error = result.err().expect("operation unexpectedly succeeded");
    assert_eq!(error.kind(), expected, "{error}");
    assert!(!error.message().trim().is_empty());
    error
}

fn addressed_block(content: impl AsRef<[u8]>) -> MerkleBlock {
    let content = content.as_ref();
    MerkleBlock::new(MerkleDigest::hash(content), content)
}

#[test]
fn persistence_export_reuses_the_csharp_golden_vector() {
    let policy = policy_with_id("golden-int-string-v1");
    let tree = Tree::new(policy.clone())
        .set_item(42, Some("forty-two".to_owned()))
        .unwrap();
    let pack = tree.export_pack();
    let block = &pack.blocks()[0];
    assert_eq!(pack.block_count(), 1);
    assert_eq!(pack.root_hash(), tree.root_hash());
    assert_eq!(block.digest(), tree.root_hash());
    assert_eq!(
        policy.domain_digest().to_string(),
        "fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"
    );
    assert_eq!(
        tree.root_hash().to_string(),
        "1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94"
    );
    assert_eq!(
        to_hex(block.content()),
        "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2000000000100000001000000040000002a0000000a01666f7274792d74776f98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb398900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
    );
}

#[test]
fn save_load_export_and_import_round_trip_exact_closure() {
    let policy = policy();
    let tree = create_tree(&policy, 513);
    let exported = tree.export_pack();
    assert!(tree.block_count() > 2);
    assert_eq!(exported.block_count(), tree.block_count());
    assert_eq!(exported.root_hash(), tree.root_hash());
    assert_eq!(exported.domain_digest(), policy.domain_digest());
    assert!(exported.contains_root_block());
    assert_eq!(exported, tree.export_pack());

    let store = InMemoryMerkleBlockStore::new();
    assert_eq!(tree.save(&store).unwrap(), tree.block_count());
    assert_eq!(tree.save(&store).unwrap(), 0);
    assert_eq!(store.len(), tree.block_count());
    let loaded = Tree::load(tree.root_hash(), &policy, &store).unwrap();
    assert_trees_equal(&tree, &loaded);
    assert_eq!(exported, loaded.export_pack());

    let imported_store = InMemoryMerkleBlockStore::new();
    let imported = Tree::import(&exported, &policy, Some(&imported_store)).unwrap();
    assert_trees_equal(&tree, &imported);
    assert_eq!(imported_store.len(), tree.block_count());
    assert_eq!(exported, imported.export_pack());

    let empty = Tree::new(policy.clone());
    let empty_pack = empty.export_pack();
    assert!(empty_pack.blocks().is_empty());
    assert_trees_equal(&empty, &Tree::import(&empty_pack, &policy, None).unwrap());
    assert_trees_equal(
        &empty,
        &Tree::load(empty.root_hash(), &policy, &InMemoryMerkleBlockStore::new()).unwrap(),
    );
}

#[test]
fn load_and_import_reject_missing_and_tampered_blocks() {
    let policy = policy();
    let tree = create_tree(&policy, 257);
    let pack = tree.export_pack();
    assert!(pack.block_count() > 1);
    let missing_block = pack.blocks().last().unwrap();
    let incomplete = MerkleBlockPack::new(
        pack.algorithm_id(),
        pack.domain_digest(),
        pack.root_hash(),
        pack.blocks()
            .iter()
            .filter(|block| block.digest() != missing_block.digest())
            .cloned(),
    )
    .unwrap();
    let missing = assert_failure(
        MerkleVerificationFailureKind::MissingBlock,
        Tree::import(&incomplete, &policy, None),
    );
    assert_eq!(missing.block_digest(), Some(missing_block.digest()));

    let store = InMemoryMerkleBlockStore::new();
    tree.save(&store).unwrap();
    assert!(store.remove(missing_block.digest()));
    assert_failure(
        MerkleVerificationFailureKind::MissingBlock,
        Tree::load(tree.root_hash(), &policy, &store),
    );

    let root = pack
        .blocks()
        .iter()
        .find(|block| block.digest() == pack.root_hash())
        .unwrap();
    let mut bytes = root.to_vec();
    *bytes.last_mut().unwrap() ^= 0x80;
    let tampered = MerkleBlock::new(root.digest(), bytes);
    let tampered_pack = MerkleBlockPack::new(
        pack.algorithm_id(),
        pack.domain_digest(),
        pack.root_hash(),
        pack.blocks().iter().map(|block| {
            if block.digest() == root.digest() {
                tampered.clone()
            } else {
                block.clone()
            }
        }),
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::DigestMismatch,
        Tree::import(&tampered_pack, &policy, None),
    );

    let tampered_store = InMemoryMerkleBlockStore::new();
    for block in tampered_pack.blocks() {
        tampered_store.put(block.clone()).unwrap();
    }
    assert_failure(
        MerkleVerificationFailureKind::DigestMismatch,
        Tree::load(tree.root_hash(), &policy, &tampered_store),
    );
}

#[test]
fn import_rejects_malformed_noncanonical_foreign_and_unsupported_envelopes() {
    let policy = policy();
    let one = Tree::new(policy.clone())
        .set_item(1, Some("one".to_owned()))
        .unwrap();
    let one_pack = one.export_pack();
    let one_block = &one_pack.blocks()[0];

    let mut trailing = one_block.to_vec();
    trailing.push(0);
    let noncanonical = addressed_block(trailing);
    let noncanonical_pack = MerkleBlockPack::new(
        MerkleSearchTreePolicy::<i32, Option<String>>::ALGORITHM_ID,
        policy.domain_digest(),
        noncanonical.digest(),
        [noncanonical],
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::NonCanonicalBlock,
        Tree::import(&noncanonical_pack, &policy, None),
    );

    let mut wrong_magic = one_block.to_vec();
    wrong_magic[0] ^= 0xff;
    let malformed = addressed_block(wrong_magic);
    let malformed_pack = MerkleBlockPack::new(
        MerkleSearchTreePolicy::<i32, Option<String>>::ALGORITHM_ID,
        policy.domain_digest(),
        malformed.digest(),
        [malformed],
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::MalformedBlock,
        Tree::import(&malformed_pack, &policy, None),
    );

    let foreign_policy = policy_with_id("foreign-wire-domain-v1");
    let foreign_tree = Tree::new(foreign_policy)
        .set_item(1, Some("one".to_owned()))
        .unwrap();
    let relabeled = MerkleBlockPack::new(
        MerkleSearchTreePolicy::<i32, Option<String>>::ALGORITHM_ID,
        policy.domain_digest(),
        foreign_tree.root_hash(),
        foreign_tree.export_pack().blocks().iter().cloned(),
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::DomainMismatch,
        Tree::import(&relabeled, &policy, None),
    );

    let unsupported = MerkleBlockPack::new(
        "mst-sha256-b16-v999",
        policy.domain_digest(),
        one.root_hash(),
        one.export_pack().blocks().iter().cloned(),
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::UnsupportedAlgorithm,
        Tree::import(&unsupported, &policy, None),
    );
}

#[test]
fn every_verification_budget_limit_is_enforced_independently() {
    let policy = policy();
    let tree = create_tree(&policy, 513);
    assert!(tree.height() > 1);
    let pack = tree.export_pack();
    let root_len = pack
        .blocks()
        .iter()
        .find(|block| block.digest() == pack.root_hash())
        .unwrap()
        .len();
    let default = MerkleVerificationBudget::default();

    let block_count = MerkleVerificationBudget::new(
        1,
        default.max_total_byte_count,
        default.max_block_byte_count,
        default.max_depth,
        default.max_entry_count,
        default.max_child_references_per_block,
        default.max_proof_query_byte_count,
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::ResourceLimitExceeded,
        Tree::import_with_budget(&pack, &policy, None, &block_count),
    );

    let total_bytes = MerkleVerificationBudget::new(
        default.max_block_count,
        root_len as u64,
        root_len,
        default.max_depth,
        default.max_entry_count,
        default.max_child_references_per_block,
        root_len,
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::ResourceLimitExceeded,
        Tree::import_with_budget(&pack, &policy, None, &total_bytes),
    );

    let per_block = MerkleVerificationBudget::new(
        default.max_block_count,
        default.max_total_byte_count,
        root_len - 1,
        default.max_depth,
        default.max_entry_count,
        default.max_child_references_per_block,
        root_len - 1,
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::ResourceLimitExceeded,
        Tree::import_with_budget(&pack, &policy, None, &per_block),
    );

    let depth = MerkleVerificationBudget::new(
        default.max_block_count,
        default.max_total_byte_count,
        default.max_block_byte_count,
        1,
        default.max_entry_count,
        default.max_child_references_per_block,
        default.max_proof_query_byte_count,
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::ResourceLimitExceeded,
        Tree::import_with_budget(&pack, &policy, None, &depth),
    );

    let entries = MerkleVerificationBudget::new(
        default.max_block_count,
        default.max_total_byte_count,
        default.max_block_byte_count,
        default.max_depth,
        1,
        default.max_child_references_per_block,
        default.max_proof_query_byte_count,
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::ResourceLimitExceeded,
        Tree::import_with_budget(&pack, &policy, None, &entries),
    );

    let child_references = MerkleVerificationBudget::new(
        default.max_block_count,
        default.max_total_byte_count,
        default.max_block_byte_count,
        default.max_depth,
        default.max_entry_count,
        1,
        default.max_proof_query_byte_count,
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::ResourceLimitExceeded,
        Tree::import_with_budget(&pack, &policy, None, &child_references),
    );

    let proof = tree.create_proof(&0).unwrap();
    let query = MerkleVerificationBudget::new(
        default.max_block_count,
        default.max_total_byte_count,
        default.max_block_byte_count,
        default.max_depth,
        default.max_entry_count,
        default.max_child_references_per_block,
        proof.query().len() - 1,
    )
    .unwrap();
    let result = Tree::verify_proof_with_budget(&proof, &policy, &query);
    assert!(!result.is_valid());
    assert_eq!(
        result.failure_kind(),
        MerkleVerificationFailureKind::ResourceLimitExceeded
    );
    assert_eq!(result.verified_block_count(), 0);
    assert_eq!(result.verified_byte_count(), 0);
}

#[test]
fn load_rejects_authenticated_count_and_interval_reference_tampering() {
    let policy = policy();
    let tree = create_tree(&policy, 2_049);
    let pack = tree.export_pack();
    let root = pack
        .blocks()
        .iter()
        .find(|block| block.digest() == pack.root_hash())
        .unwrap();

    let mut wrong_count_bytes = root.to_vec();
    let count = i32::from_be_bytes(wrong_count_bytes[38..42].try_into().unwrap());
    wrong_count_bytes[38..42].copy_from_slice(&(count + 1).to_be_bytes());
    let wrong_count = addressed_block(wrong_count_bytes);
    let wrong_count_pack = replace_root(&pack, wrong_count.clone());
    assert_failure(
        MerkleVerificationFailureKind::InvalidReference,
        Tree::import(&wrong_count_pack, &policy, None),
    );

    let mut crossed_bytes = root.to_vec();
    let child_offset = child_digest_offset(&crossed_bytes);
    let child_count = child_digest_count(&crossed_bytes);
    let empty = policy.empty_digest();
    let nonempty = (0..child_count)
        .filter(|index| {
            MerkleDigest::from_bytes(
                &crossed_bytes[child_offset + index * 32..child_offset + (index + 1) * 32],
            )
            .unwrap()
                != empty
        })
        .collect::<Vec<_>>();
    assert!(nonempty.len() >= 2);
    let first = child_offset + nonempty[0] * 32;
    let second = child_offset + nonempty[1] * 32;
    for byte in 0..32 {
        crossed_bytes.swap(first + byte, second + byte);
    }
    let crossed = addressed_block(crossed_bytes);
    let crossed_pack = replace_root(&pack, crossed);
    assert_failure(
        MerkleVerificationFailureKind::InvalidReference,
        Tree::import(&crossed_pack, &policy, None),
    );
}

#[test]
fn save_and_import_preflight_late_conflicts_without_partial_writes() {
    let policy = policy();
    let tree = create_tree(&policy, 257);
    let pack = tree.export_pack();
    let late = pack.blocks().last().unwrap();
    let conflicting = MerkleBlock::new(late.digest(), [0xde, 0xad, 0xbe, 0xef]);

    let save_store = RecordingStore::default();
    save_store.seed_unsafe(conflicting.clone());
    assert_failure(
        MerkleVerificationFailureKind::ConflictingBlock,
        tree.save(&save_store),
    );
    assert_eq!(save_store.put_call_count(), 0);
    assert_eq!(save_store.len(), 1);
    assert_eq!(save_store.get(late.digest()), Some(conflicting.clone()));

    let import_store = RecordingStore::default();
    import_store.seed_unsafe(conflicting.clone());
    assert_failure(
        MerkleVerificationFailureKind::ConflictingBlock,
        Tree::import(&pack, &policy, Some(&import_store)),
    );
    assert_eq!(import_store.put_call_count(), 0);
    assert_eq!(import_store.len(), 1);
    assert_eq!(import_store.get(late.digest()), Some(conflicting));

    let sentinel = addressed_block(b"sentinel");
    let failure_store = RecordingStore::default();
    failure_store.seed_unsafe(sentinel.clone());
    let root = pack
        .blocks()
        .iter()
        .find(|block| block.digest() == pack.root_hash())
        .unwrap();
    let mut broken = root.to_vec();
    *broken.last_mut().unwrap() ^= 1;
    let broken_root = MerkleBlock::new(root.digest(), broken);
    let broken_pack = MerkleBlockPack::new(
        pack.algorithm_id(),
        pack.domain_digest(),
        pack.root_hash(),
        pack.blocks().iter().map(|block| {
            if block.digest() == root.digest() {
                broken_root.clone()
            } else {
                block.clone()
            }
        }),
    )
    .unwrap();
    assert_failure(
        MerkleVerificationFailureKind::DigestMismatch,
        Tree::import(&broken_pack, &policy, Some(&failure_store)),
    );
    assert_eq!(failure_store.put_call_count(), 0);
    assert_eq!(failure_store.len(), 1);
    assert_eq!(failure_store.get(sentinel.digest()), Some(sentinel));
}

#[test]
fn sync_plans_and_packs_cover_complete_partial_and_iterative_repair() {
    let policy = policy();
    let target = create_tree(&policy, 513);
    let local = Tree::new(policy.clone());

    let empty_receiver = InMemoryMerkleBlockStore::new();
    let complete_pack = target.create_sync_pack(&empty_receiver);
    assert_eq!(complete_pack, target.export_pack());
    let complete = Tree::import(&complete_pack, &policy, Some(&empty_receiver)).unwrap();
    assert_trees_equal(&target, &complete);
    assert_eq!(empty_receiver.len(), target.block_count());

    let partial_receiver = InMemoryMerkleBlockStore::new();
    target.save(&partial_receiver).unwrap();
    let missing = target.export_pack().blocks().last().unwrap().clone();
    assert_ne!(missing.digest(), target.root_hash());
    assert!(partial_receiver.remove(missing.digest()));
    let plan = target.plan_sync(&local, &partial_receiver).unwrap();
    assert!(!plan.roots_match());
    assert!(plan.requires_blocks());
    assert_eq!(plan.requested_blocks(), [missing.digest()]);
    assert!(plan.examined_block_count() > 0);
    assert!(plan.examined_byte_count() > 0);
    let partial_pack = target
        .export_pack_for(plan.requested_blocks().iter().copied())
        .unwrap();
    assert!(!partial_pack.contains_root_block());
    assert_eq!(partial_pack.blocks(), [missing]);
    let repaired = Tree::import(&partial_pack, &policy, Some(&partial_receiver)).unwrap();
    assert_trees_equal(&target, &repaired);
    assert_eq!(partial_receiver.len(), target.block_count());
    let converged = target.plan_sync(&repaired, &partial_receiver).unwrap();
    assert!(converged.roots_match());
    assert!(!converged.requires_blocks());
    assert_eq!(converged.examined_block_count(), 0);
    assert_eq!(converged.examined_byte_count(), 0);

    // An empty receiver requests only the root first; each imported frontier reveals the next.
    let frontier_store = InMemoryMerkleBlockStore::new();
    let mut rounds = 0;
    loop {
        let plan = target.plan_sync(&local, &frontier_store).unwrap();
        if !plan.requires_blocks() {
            break;
        }
        rounds += 1;
        let pack = target
            .export_pack_for(plan.requested_blocks().iter().copied())
            .unwrap();
        for block in pack.blocks() {
            frontier_store.put(block.clone()).unwrap();
        }
    }
    assert!(rounds >= target.height());
    assert_trees_equal(
        &target,
        &Tree::load(target.root_hash(), &policy, &frontier_store).unwrap(),
    );
}

#[test]
fn point_range_and_empty_proofs_verify_exact_msp2_claims() {
    let policy = policy();
    let tree = create_tree(&policy, 513);
    let membership = tree.create_proof(&0).unwrap();
    assert_eq!(membership.kind(), MerkleProofKind::Membership);
    assert_eq!(&membership.query()[..5], b"MSP2\0");
    assert_proof_valid(&membership, &policy);
    let nonmembership = tree.create_proof(&10_000).unwrap();
    assert_eq!(nonmembership.kind(), MerkleProofKind::NonMembership);
    assert_eq!(&nonmembership.query()[..5], b"MSP2\x01");
    assert_proof_valid(&nonmembership, &policy);
    let range = tree.create_range_proof(&-20, &20).unwrap();
    assert_eq!(range.kind(), MerkleProofKind::Range);
    assert_eq!(&range.query()[..5], b"MSP2\x02");
    assert_proof_valid(&range, &policy);
    assert!(range.steps().len() >= membership.steps().len());

    let empty = Tree::new(policy.clone());
    assert_proof_valid(&empty.create_proof(&1).unwrap(), &policy);
    assert_proof_valid(&empty.create_range_proof(&-1, &1).unwrap(), &policy);
}

#[test]
fn proof_query_limit_fails_before_any_codec_or_block_decode() {
    let key_counts = Arc::new(CodecCounts::default());
    let value_counts = Arc::new(CodecCounts::default());
    let policy = MerkleSearchTreePolicy::natural(
        "proof-query-budget-v1",
        CountingCodec::new(Int32MerkleCodec, Arc::clone(&key_counts)),
        CountingCodec::new(NullableUtf8MerkleCodec, Arc::clone(&value_counts)),
    )
    .unwrap();
    for empty in [false, true] {
        let tree = if empty {
            Tree::new(policy.clone())
        } else {
            Tree::new(policy.clone())
                .set_item(1, Some("one".to_owned()))
                .unwrap()
        };
        let proof = tree.create_proof(&1).unwrap();
        key_counts.reset();
        value_counts.reset();
        let defaults = MerkleVerificationBudget::default();
        let budget = MerkleVerificationBudget::new(
            defaults.max_block_count,
            1 << 20,
            1 << 20,
            defaults.max_depth,
            defaults.max_entry_count,
            defaults.max_child_references_per_block,
            proof.query().len() - 1,
        )
        .unwrap();
        let result = Tree::verify_proof_with_budget(&proof, &policy, &budget);
        assert!(!result.is_valid());
        assert_eq!(
            result.failure_kind(),
            MerkleVerificationFailureKind::ResourceLimitExceeded
        );
        assert_eq!(result.verified_block_count(), 0);
        assert_eq!(result.verified_byte_count(), 0);
        assert_eq!(key_counts.snapshot(), (0, 0));
        assert_eq!(value_counts.snapshot(), (0, 0));
    }
}

#[test]
fn proof_structure_limits_fail_before_verifier_allocation_or_decode() {
    let key_counts = Arc::new(CodecCounts::default());
    let value_counts = Arc::new(CodecCounts::default());
    let policy = MerkleSearchTreePolicy::natural(
        "proof-structure-budget-v1",
        CountingCodec::new(Int32MerkleCodec, Arc::clone(&key_counts)),
        CountingCodec::new(NullableUtf8MerkleCodec, Arc::clone(&value_counts)),
    )
    .unwrap();
    let proof = create_tree(&policy, 513).create_proof(&0).unwrap();
    assert!(proof.steps().len() > 1);

    key_counts.reset();
    value_counts.reset();
    let defaults = MerkleVerificationBudget::default();
    let step_budget = MerkleVerificationBudget::new(
        proof.steps().len() - 1,
        defaults.max_total_byte_count,
        defaults.max_block_byte_count,
        defaults.max_depth,
        defaults.max_entry_count,
        defaults.max_child_references_per_block,
        defaults.max_proof_query_byte_count,
    )
    .unwrap();
    let step_result = Tree::verify_proof_with_budget(&proof, &policy, &step_budget);
    assert_early_proof_limit_failure(
        &step_result,
        proof.query().len() as u64,
        &key_counts,
        &value_counts,
    );

    let mut expanded_steps = proof.steps().to_vec();
    expanded_steps[0] = MerkleProofStep::new(expanded_steps[0].block().clone(), [0, 1]).unwrap();
    let expanded_proof = rebuild_proof(&proof, proof.query(), expanded_steps);
    key_counts.reset();
    value_counts.reset();
    let expansion_budget = MerkleVerificationBudget::new(
        defaults.max_block_count,
        defaults.max_total_byte_count,
        defaults.max_block_byte_count,
        defaults.max_depth,
        defaults.max_entry_count,
        1,
        defaults.max_proof_query_byte_count,
    )
    .unwrap();
    let expansion_result =
        Tree::verify_proof_with_budget(&expanded_proof, &policy, &expansion_budget);
    assert_early_proof_limit_failure(
        &expansion_result,
        expanded_proof.query().len() as u64,
        &key_counts,
        &value_counts,
    );
}

#[test]
fn proof_verification_rejects_tampered_queries_steps_and_expansions() {
    let policy = policy();
    let tree = create_tree(&policy, 513);
    let proof = tree.create_proof(&0).unwrap();
    assert!(!proof.steps().is_empty());

    let mut tampered_steps = proof.steps().to_vec();
    let first = &proof.steps()[0];
    let mut changed = first.block().to_vec();
    *changed.last_mut().unwrap() ^= 0x40;
    tampered_steps[0] = MerkleProofStep::new(
        MerkleBlock::new(first.block().digest(), changed),
        first.expanded_child_indexes().iter().copied(),
    )
    .unwrap();
    assert_proof_failure(
        &rebuild_proof(&proof, proof.query(), tampered_steps),
        &policy,
        MerkleVerificationFailureKind::DigestMismatch,
    );

    let mut trailing = proof.query().to_vec();
    trailing.push(0);
    assert_proof_failure(
        &rebuild_proof(&proof, trailing, proof.steps().to_vec()),
        &policy,
        MerkleVerificationFailureKind::ProofMismatch,
    );
    let mut changed_value = proof.query().to_vec();
    *changed_value.last_mut().unwrap() ^= 1;
    assert_proof_failure(
        &rebuild_proof(&proof, changed_value, proof.steps().to_vec()),
        &policy,
        MerkleVerificationFailureKind::ProofMismatch,
    );

    let proof_digests = proof
        .steps()
        .iter()
        .map(|step| step.block().digest())
        .collect::<std::collections::HashSet<_>>();
    let extra = tree
        .export_pack()
        .blocks()
        .iter()
        .find(|block| !proof_digests.contains(&block.digest()))
        .unwrap()
        .clone();
    let mut extra_steps = proof.steps().to_vec();
    extra_steps.push(MerkleProofStep::new(extra, []).unwrap());
    assert_proof_failure(
        &rebuild_proof(&proof, proof.query(), extra_steps),
        &policy,
        MerkleVerificationFailureKind::ProofMismatch,
    );

    let mut wrong_expansion = proof.steps().to_vec();
    wrong_expansion[0] = MerkleProofStep::new(first.block().clone(), []).unwrap();
    assert_proof_failure(
        &rebuild_proof(&proof, proof.query(), wrong_expansion),
        &policy,
        MerkleVerificationFailureKind::ProofMismatch,
    );

    let mut omitted = proof.steps().to_vec();
    omitted.pop();
    assert_proof_failure(
        &rebuild_proof(&proof, proof.query(), omitted),
        &policy,
        MerkleVerificationFailureKind::MissingBlock,
    );
}

#[test]
fn three_way_merge_combines_edits_and_withholds_partial_conflicted_trees() {
    let policy = policy();
    let base = Tree::new(policy)
        .set_item(1, Some("one".to_owned()))
        .unwrap()
        .set_item(2, Some("two".to_owned()))
        .unwrap()
        .set_item(3, Some("three".to_owned()))
        .unwrap();
    let left = base.set_item(1, Some("ONE".to_owned())).unwrap();
    let right = base.set_item(2, Some("TWO".to_owned())).unwrap();
    let mut must_not_run = |_conflict: &MerkleThreeWayMergeConflict<i32, Option<String>>| {
        panic!("resolver ran for disjoint edits")
    };
    let disjoint = Tree::merge(&base, &left, &right, Some(&mut must_not_run)).unwrap();
    let disjoint = disjoint.merged_tree().unwrap();
    assert_eq!(disjoint.get(&1).unwrap().as_deref(), Some("ONE"));
    assert_eq!(disjoint.get(&2).unwrap().as_deref(), Some("TWO"));
    assert_eq!(disjoint.get(&3).unwrap().as_deref(), Some("three"));

    let same_left = base.set_item(3, Some("THREE".to_owned())).unwrap();
    let same_right = base.set_item(3, Some("THREE".to_owned())).unwrap();
    let same = Tree::merge(&base, &same_left, &same_right, Some(&mut must_not_run)).unwrap();
    assert_eq!(
        same.merged_tree().unwrap().get(&3).unwrap().as_deref(),
        Some("THREE")
    );

    let conflicting_left = base.set_item(1, Some("left".to_owned())).unwrap();
    let conflicting_right = base.set_item(1, Some("right".to_owned())).unwrap();
    let unresolved = Tree::merge(&base, &conflicting_left, &conflicting_right, None).unwrap();
    assert!(!unresolved.is_success());
    assert!(unresolved.merged_tree().is_none());
    let conflict = &unresolved.unresolved_conflicts()[0];
    assert_eq!(*conflict.key, 1);
    assert_eq!(conflict.base.value().unwrap().as_deref(), Some("one"));
    assert_eq!(conflict.left.value().unwrap().as_deref(), Some("left"));
    assert_eq!(conflict.right.value().unwrap().as_deref(), Some("right"));

    let mut set_value = |_conflict: &MerkleThreeWayMergeConflict<i32, Option<String>>| {
        MerkleMergeResolution::SetValue(Some("resolved".to_owned()))
    };
    let resolved = Tree::merge(
        &base,
        &conflicting_left,
        &conflicting_right,
        Some(&mut set_value),
    )
    .unwrap();
    assert_eq!(
        resolved.merged_tree().unwrap().get(&1).unwrap().as_deref(),
        Some("resolved")
    );
    let mut use_right = |_conflict: &MerkleThreeWayMergeConflict<i32, Option<String>>| {
        MerkleMergeResolution::UseRight
    };
    assert_eq!(
        Tree::merge(
            &base,
            &conflicting_left,
            &conflicting_right,
            Some(&mut use_right)
        )
        .unwrap()
        .merged_tree()
        .unwrap()
        .get(&1)
        .unwrap()
        .as_deref(),
        Some("right")
    );
    let mut delete = |_conflict: &MerkleThreeWayMergeConflict<i32, Option<String>>| {
        MerkleMergeResolution::Delete
    };
    assert!(
        Tree::merge(
            &base,
            &conflicting_left,
            &conflicting_right,
            Some(&mut delete)
        )
        .unwrap()
        .merged_tree()
        .unwrap()
        .get(&1)
        .is_none()
    );
}

#[test]
fn merge_distinguishes_present_none_from_deletion() {
    let policy = policy();
    let base = Tree::new(policy)
        .set_item(1, Some("base".to_owned()))
        .unwrap();
    let present_none = base.set_item(1, None).unwrap();
    let deleted = base.remove(&1);
    let unresolved = Tree::merge(&base, &present_none, &deleted, None).unwrap();
    let conflict = &unresolved.unresolved_conflicts()[0];
    assert!(conflict.left.is_present());
    assert_eq!(conflict.left.value(), Some(&None));
    assert!(!conflict.right.is_present());
    assert_eq!(conflict.right.value(), None);

    let mut keep_none = |_conflict: &MerkleThreeWayMergeConflict<i32, Option<String>>| {
        MerkleMergeResolution::SetValue(None)
    };
    let merged = Tree::merge(&base, &present_none, &deleted, Some(&mut keep_none))
        .unwrap()
        .into_merged_tree()
        .unwrap();
    assert!(merged.contains_key(&1));
    assert_eq!(merged.get(&1), Some(&None));
}

#[test]
fn in_memory_store_is_thread_safe_idempotent_and_conflict_rejecting() {
    let policy = policy();
    let tree = create_tree(&policy, 513);
    let blocks = Arc::new(tree.export_pack().blocks().to_vec());
    let store = Arc::new(InMemoryMerkleBlockStore::new());
    let handles = (0..8)
        .map(|_| {
            let blocks = Arc::clone(&blocks);
            let store = Arc::clone(&store);
            thread::spawn(move || {
                for block in blocks.iter() {
                    store.put(block.clone()).unwrap();
                    assert!(store.contains(block.digest()));
                    assert_eq!(store.get(block.digest()), Some(block.clone()));
                }
            })
        })
        .collect::<Vec<_>>();
    for handle in handles {
        handle.join().unwrap();
    }
    assert_eq!(store.len(), blocks.len());
    let mut expected = blocks.iter().map(MerkleBlock::digest).collect::<Vec<_>>();
    expected.sort_unstable();
    assert_eq!(store.digests(), expected);
    let first = &blocks[0];
    let conflict = MerkleBlock::new(first.digest(), b"different");
    assert_failure(
        MerkleVerificationFailureKind::ConflictingBlock,
        store.put(conflict),
    );
}

#[test]
fn packs_and_explicit_exports_reject_duplicate_or_unknown_addresses() {
    let policy = policy();
    let tree = create_tree(&policy, 65);
    let pack = tree.export_pack();
    let first = pack.blocks()[0].clone();
    assert_failure(
        MerkleVerificationFailureKind::DuplicateBlock,
        MerkleBlockPack::new(
            pack.algorithm_id(),
            pack.domain_digest(),
            pack.root_hash(),
            [first.clone(), first.clone()],
        ),
    );
    assert_failure(
        MerkleVerificationFailureKind::DuplicateBlock,
        tree.export_pack_for([first.digest(), first.digest()]),
    );
    let unknown = MerkleDigest::hash(b"unknown block");
    assert_failure(
        MerkleVerificationFailureKind::MissingBlock,
        tree.export_pack_for([unknown]),
    );
}

#[test]
fn proof_envelopes_reject_foreign_roots_domains_algorithms_and_duplicates() {
    let policy = policy();
    let tree = create_tree(&policy, 257);
    let proof = tree.create_proof(&0).unwrap();

    let wrong_root = MerkleProof::new(
        proof.algorithm_id(),
        proof.domain_digest(),
        MerkleDigest::hash(b"another root"),
        proof.kind(),
        proof.query(),
        proof.steps().to_vec(),
    )
    .unwrap();
    assert_proof_failure(
        &wrong_root,
        &policy,
        MerkleVerificationFailureKind::RootMismatch,
    );

    let wrong_domain = MerkleProof::new(
        proof.algorithm_id(),
        MerkleDigest::hash(b"another domain"),
        proof.root_hash(),
        proof.kind(),
        proof.query(),
        proof.steps().to_vec(),
    )
    .unwrap();
    assert_proof_failure(
        &wrong_domain,
        &policy,
        MerkleVerificationFailureKind::DomainMismatch,
    );

    let wrong_algorithm = MerkleProof::new(
        "mst-sha256-b16-v999",
        proof.domain_digest(),
        proof.root_hash(),
        proof.kind(),
        proof.query(),
        proof.steps().to_vec(),
    )
    .unwrap();
    assert_proof_failure(
        &wrong_algorithm,
        &policy,
        MerkleVerificationFailureKind::UnsupportedAlgorithm,
    );

    let step = proof.steps()[0].clone();
    assert_failure(
        MerkleVerificationFailureKind::DuplicateBlock,
        MerkleProof::new(
            proof.algorithm_id(),
            proof.domain_digest(),
            proof.root_hash(),
            proof.kind(),
            proof.query(),
            [step.clone(), step],
        ),
    );
    assert_failure(
        MerkleVerificationFailureKind::ProofMismatch,
        MerkleProofStep::new(proof.steps()[0].block().clone(), [0, 0]),
    );
}

#[derive(Debug, Eq, PartialEq)]
struct NonCloneValue(i32);

#[derive(Clone, Copy)]
struct NonCloneCodec;

impl MerkleCodec<NonCloneValue> for NonCloneCodec {
    fn encoding_id(&self) -> &str {
        "persistence-non-clone-v1"
    }

    fn encode(&self, value: &NonCloneValue) -> Result<Vec<u8>, MerkleCodecError> {
        Int32MerkleCodec.encode(&value.0)
    }

    fn decode(&self, encoding: &[u8]) -> Result<NonCloneValue, MerkleCodecError> {
        Ok(NonCloneValue(Int32MerkleCodec.decode(encoding)?))
    }
}

#[test]
fn persistence_and_merge_do_not_require_payload_clone() {
    type NonCloneTree = MerkleSearchTree<i32, NonCloneValue>;
    let policy = MerkleSearchTreePolicy::natural(
        "persistence-non-clone-v1",
        Int32MerkleCodec,
        NonCloneCodec,
    )
    .unwrap();
    let base = NonCloneTree::new(policy.clone())
        .set_item(1, NonCloneValue(10))
        .unwrap()
        .set_item(2, NonCloneValue(20))
        .unwrap();
    let store = InMemoryMerkleBlockStore::new();
    base.save(&store).unwrap();
    let loaded = NonCloneTree::load(base.root_hash(), &policy, &store).unwrap();
    assert_eq!(loaded.get(&1), Some(&NonCloneValue(10)));
    let imported = NonCloneTree::import(&base.export_pack(), &policy, None).unwrap();
    assert_eq!(imported.get(&2), Some(&NonCloneValue(20)));

    let left = base.set_item(1, NonCloneValue(11)).unwrap();
    let right = base.set_item(2, NonCloneValue(22)).unwrap();
    let merged = NonCloneTree::merge(&base, &left, &right, None)
        .unwrap()
        .into_merged_tree()
        .unwrap();
    assert_eq!(merged.get(&1), Some(&NonCloneValue(11)));
    assert_eq!(merged.get(&2), Some(&NonCloneValue(22)));
}

#[test]
fn verification_budget_construction_rejects_invalid_relationships() {
    assert!(MerkleVerificationBudget::new(0, 1, 1, 1, 1, 1, 1).is_err());
    assert!(MerkleVerificationBudget::new(1, 0, 1, 1, 1, 1, 1).is_err());
    assert!(MerkleVerificationBudget::new(1, 10, 11, 1, 1, 1, 1).is_err());
    assert!(MerkleVerificationBudget::new(1, 10, 1, 1, 1, 1, 11).is_err());
    let six = MerkleVerificationBudget::with_six_limits(1, 10, 7, 1, 1, 1).unwrap();
    assert_eq!(six.max_proof_query_byte_count, 7);
}

fn assert_proof_valid(proof: &MerkleProof, policy: &MerkleSearchTreePolicy<i32, Option<String>>) {
    let result = Tree::verify_proof(proof, policy);
    assert!(result.is_valid(), "{:?}", result.failure_message());
    assert_eq!(result.failure_kind(), MerkleVerificationFailureKind::None);
    assert_eq!(result.computed_root_hash(), Some(proof.root_hash()));
    assert_eq!(result.verified_block_count(), proof.steps().len());
    assert_eq!(result.verified_byte_count(), proof.total_byte_count());
}

fn assert_proof_failure(
    proof: &MerkleProof,
    policy: &MerkleSearchTreePolicy<i32, Option<String>>,
    expected: MerkleVerificationFailureKind,
) {
    let result = Tree::verify_proof(proof, policy);
    assert!(!result.is_valid());
    assert_eq!(
        result.failure_kind(),
        expected,
        "{:?}",
        result.failure_message()
    );
    assert!(
        result
            .failure_message()
            .is_some_and(|message| !message.trim().is_empty())
    );
}

fn assert_early_proof_limit_failure(
    result: &tools_data_structures_hamt::MerkleProofVerificationResult,
    expected_query_byte_count: u64,
    key_counts: &CodecCounts,
    value_counts: &CodecCounts,
) {
    assert!(!result.is_valid());
    assert_eq!(
        result.failure_kind(),
        MerkleVerificationFailureKind::ResourceLimitExceeded
    );
    assert_eq!(result.verified_block_count(), 0);
    assert_eq!(result.verified_byte_count(), expected_query_byte_count);
    assert_eq!(key_counts.snapshot(), (0, 0));
    assert_eq!(value_counts.snapshot(), (0, 0));
}

fn rebuild_proof(
    source: &MerkleProof,
    query: impl AsRef<[u8]>,
    steps: Vec<MerkleProofStep>,
) -> MerkleProof {
    MerkleProof::new(
        source.algorithm_id(),
        source.domain_digest(),
        source.root_hash(),
        source.kind(),
        query,
        steps,
    )
    .unwrap()
}

fn replace_root(pack: &MerkleBlockPack, replacement: MerkleBlock) -> MerkleBlockPack {
    MerkleBlockPack::new(
        pack.algorithm_id(),
        pack.domain_digest(),
        replacement.digest(),
        pack.blocks().iter().map(|block| {
            if block.digest() == pack.root_hash() {
                replacement.clone()
            } else {
                block.clone()
            }
        }),
    )
    .unwrap()
}

fn child_digest_offset(block: &[u8]) -> usize {
    let entry_count = i32::from_be_bytes(block[42..46].try_into().unwrap()) as usize;
    let mut offset = 46;
    for _ in 0..entry_count {
        let key_length = i32::from_be_bytes(block[offset..offset + 4].try_into().unwrap()) as usize;
        offset += 4 + key_length;
        let value_length =
            i32::from_be_bytes(block[offset..offset + 4].try_into().unwrap()) as usize;
        offset += 4 + value_length;
    }
    offset
}

fn child_digest_count(block: &[u8]) -> usize {
    i32::from_be_bytes(block[42..46].try_into().unwrap()) as usize + 1
}

fn to_hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

#[derive(Default)]
struct RecordingState {
    blocks: HashMap<MerkleDigest, MerkleBlock>,
    put_calls: usize,
}

#[derive(Default)]
struct RecordingStore {
    state: Mutex<RecordingState>,
}

impl RecordingStore {
    fn lock(&self) -> MutexGuard<'_, RecordingState> {
        self.state.lock().unwrap_or_else(|error| error.into_inner())
    }

    fn seed_unsafe(&self, block: MerkleBlock) {
        self.lock().blocks.insert(block.digest(), block);
    }

    fn put_call_count(&self) -> usize {
        self.lock().put_calls
    }
}

impl MerkleBlockStore for RecordingStore {
    fn len(&self) -> usize {
        self.lock().blocks.len()
    }

    fn digests(&self) -> Vec<MerkleDigest> {
        let mut result = self.lock().blocks.keys().copied().collect::<Vec<_>>();
        result.sort_unstable();
        result
    }

    fn contains(&self, digest: MerkleDigest) -> bool {
        self.lock().blocks.contains_key(&digest)
    }

    fn get(&self, digest: MerkleDigest) -> Option<MerkleBlock> {
        self.lock().blocks.get(&digest).cloned()
    }

    fn put(&self, block: MerkleBlock) -> Result<bool, MerkleVerificationError> {
        let mut state = self.lock();
        state.put_calls += 1;
        if let Some(existing) = state.blocks.get(&block.digest()) {
            if existing == &block {
                return Ok(false);
            }
            return Err(MerkleVerificationError::conflicting_block(
                block.digest(),
                format!("conflicting bytes for '{}'", block.digest()),
            ));
        }
        state.blocks.insert(block.digest(), block);
        Ok(true)
    }

    fn remove(&self, digest: MerkleDigest) -> bool {
        self.lock().blocks.remove(&digest).is_some()
    }

    fn clear(&self) {
        self.lock().blocks.clear();
    }
}

#[derive(Default)]
struct CodecCounts {
    encode: AtomicUsize,
    decode: AtomicUsize,
}

impl CodecCounts {
    fn reset(&self) {
        self.encode.store(0, Ordering::SeqCst);
        self.decode.store(0, Ordering::SeqCst);
    }

    fn snapshot(&self) -> (usize, usize) {
        (
            self.encode.load(Ordering::SeqCst),
            self.decode.load(Ordering::SeqCst),
        )
    }
}

struct CountingCodec<C> {
    inner: C,
    counts: Arc<CodecCounts>,
}

impl<C> CountingCodec<C> {
    fn new(inner: C, counts: Arc<CodecCounts>) -> Self {
        Self { inner, counts }
    }
}

impl<T, C: MerkleCodec<T>> MerkleCodec<T> for CountingCodec<C> {
    fn encoding_id(&self) -> &str {
        self.inner.encoding_id()
    }

    fn encode(&self, value: &T) -> Result<Vec<u8>, MerkleCodecError> {
        self.counts.encode.fetch_add(1, Ordering::SeqCst);
        self.inner.encode(value)
    }

    fn decode(&self, encoding: &[u8]) -> Result<T, MerkleCodecError> {
        self.counts.decode.fetch_add(1, Ordering::SeqCst);
        self.inner.decode(encoding)
    }
}
