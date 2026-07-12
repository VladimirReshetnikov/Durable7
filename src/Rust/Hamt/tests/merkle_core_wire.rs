use std::cmp::Ordering;
use std::collections::BTreeMap;
use std::sync::Arc;
use std::thread;

use tools_data_structures_hamt::{
    Int32MerkleCodec, Int64MerkleCodec, MerkleCodec, MerkleCodecError, MerkleDigest,
    MerkleDigestParseError, MerkleDigestWriteError, MerkleMapDifference, MerklePolicyError,
    MerkleSearchTree, MerkleSearchTreePolicy, NullableBytesMerkleCodec, NullableUtf8MerkleCodec,
    Rfc4122Guid, Rfc4122GuidMerkleCodec,
};

type StringTree = MerkleSearchTree<i32, Option<String>>;

fn string_policy() -> MerkleSearchTreePolicy<i32, Option<String>> {
    MerkleSearchTreePolicy::natural(
        "golden-int-string-v1",
        Int32MerkleCodec,
        NullableUtf8MerkleCodec,
    )
    .unwrap()
}

fn decode_hex(value: &str) -> Vec<u8> {
    assert_eq!(value.len() % 2, 0);
    value
        .as_bytes()
        .chunks_exact(2)
        .map(|pair| {
            let digit = |byte| match byte {
                b'0'..=b'9' => byte - b'0',
                b'a'..=b'f' => byte - b'a' + 10,
                b'A'..=b'F' => byte - b'A' + 10,
                _ => panic!("non-hexadecimal test vector"),
            };
            (digit(pair[0]) << 4) | digit(pair[1])
        })
        .collect()
}

#[test]
fn built_in_codecs_match_strict_cross_language_vectors() {
    assert_eq!(Int32MerkleCodec.encoding_id(), "i32-be-v1");
    assert_eq!(Int32MerkleCodec.encode(&0x0102_0304).unwrap(), [1, 2, 3, 4]);
    assert_eq!(Int32MerkleCodec.encode(&i32::MIN).unwrap(), [0x80, 0, 0, 0]);
    assert_eq!(Int32MerkleCodec.decode(&[0xff; 4]).unwrap(), -1);
    for malformed in [&[][..], &[0][..], &[0; 3][..], &[0; 5][..]] {
        assert!(Int32MerkleCodec.decode(malformed).is_err());
    }

    assert_eq!(Int64MerkleCodec.encoding_id(), "i64-be-v1");
    assert_eq!(
        Int64MerkleCodec.encode(&0x0102_0304_0506_0708).unwrap(),
        [1, 2, 3, 4, 5, 6, 7, 8]
    );
    assert_eq!(Int64MerkleCodec.decode(&[0xff; 8]).unwrap(), -1);
    assert!(Int64MerkleCodec.decode(&[0; 7]).is_err());
    assert!(Int64MerkleCodec.decode(&[0; 9]).is_err());

    let text = Some("Aé😀".to_owned());
    assert_eq!(
        NullableUtf8MerkleCodec.encode(&text).unwrap(),
        [1, b'A', 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80]
    );
    assert_eq!(
        NullableUtf8MerkleCodec
            .decode(&[1, b'A', 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80])
            .unwrap(),
        text
    );
    assert_eq!(NullableUtf8MerkleCodec.encode(&None).unwrap(), [0]);
    assert_eq!(NullableUtf8MerkleCodec.decode(&[0]).unwrap(), None);
    for malformed in [
        &[][..],
        &[0, 0][..],
        &[2][..],
        &[1, 0xc0, 0x80][..],
        &[1, 0xe2, 0x82][..],
        &[1, 0xed, 0xa0, 0x80][..],
    ] {
        assert!(NullableUtf8MerkleCodec.decode(malformed).is_err());
    }

    let bytes = Some(vec![0, 1, 0xff]);
    assert_eq!(
        NullableBytesMerkleCodec.encode(&bytes).unwrap(),
        [1, 0, 1, 0xff]
    );
    assert_eq!(
        NullableBytesMerkleCodec.decode(&[1, 0, 1, 0xff]).unwrap(),
        bytes
    );
    assert_eq!(NullableBytesMerkleCodec.decode(&[0]).unwrap(), None);
    assert!(NullableBytesMerkleCodec.decode(&[]).is_err());
    assert!(NullableBytesMerkleCodec.decode(&[0, 1]).is_err());
    assert!(NullableBytesMerkleCodec.decode(&[2]).is_err());

    let guid_bytes = decode_hex("00112233445566778899aabbccddeeff");
    let guid = Rfc4122Guid::from_bytes(guid_bytes.clone().try_into().unwrap());
    assert_eq!(Rfc4122GuidMerkleCodec.encoding_id(), "guid-rfc4122-v1");
    assert_eq!(Rfc4122GuidMerkleCodec.encode(&guid).unwrap(), guid_bytes);
    assert_eq!(
        Rfc4122GuidMerkleCodec.decode(guid.as_bytes()).unwrap(),
        guid
    );
    assert!(Rfc4122GuidMerkleCodec.decode(&[0; 15]).is_err());
    assert!(Rfc4122GuidMerkleCodec.decode(&[0; 17]).is_err());
}

#[test]
fn digest_parsing_and_writing_are_exact_and_atomic() {
    let bytes = std::array::from_fn::<_, 32, _>(|index| index as u8);
    let digest = MerkleDigest::from_bytes(&bytes).unwrap();
    let hex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    assert_eq!(digest.to_string(), hex);
    assert_eq!(MerkleDigest::from_hex(&hex.to_uppercase()).unwrap(), digest);
    assert_eq!(hex.parse::<MerkleDigest>().unwrap(), digest);
    assert_eq!(digest.to_bytes(), bytes);
    assert_eq!(MerkleDigest::try_from_bytes(&bytes), Some(digest));
    assert_eq!(MerkleDigest::try_from_bytes(&bytes[..31]), None);
    assert_eq!(
        MerkleDigest::from_bytes(&bytes[..31]),
        Err(MerkleDigestParseError::InvalidBinaryLength(31))
    );
    assert_eq!(
        MerkleDigest::from_hex("00"),
        Err(MerkleDigestParseError::InvalidHexLength(2))
    );
    let mut invalid = hex.to_owned();
    invalid.replace_range(17..18, "x");
    assert_eq!(
        MerkleDigest::from_hex(&invalid),
        Err(MerkleDigestParseError::InvalidHexCharacter(17))
    );

    let mut destination = [0xaa; 40];
    digest.write_bytes(&mut destination).unwrap();
    assert_eq!(&destination[..32], &bytes);
    assert_eq!(&destination[32..], &[0xaa; 8]);
    let mut short = [0xbb; 31];
    assert_eq!(
        digest.write_bytes(&mut short),
        Err(MerkleDigestWriteError {
            required: 32,
            actual: 31,
        })
    );
    assert_eq!(short, [0xbb; 31]);
}

#[derive(Clone, Copy)]
struct NamedIntCodec(&'static str);

impl MerkleCodec<i32> for NamedIntCodec {
    fn encoding_id(&self) -> &str {
        self.0
    }

    fn encode(&self, value: &i32) -> Result<Vec<u8>, MerkleCodecError> {
        Int32MerkleCodec.encode(value)
    }

    fn decode(&self, encoding: &[u8]) -> Result<i32, MerkleCodecError> {
        Int32MerkleCodec.decode(encoding)
    }
}

#[test]
fn policy_binds_algorithm_semantics_and_codec_versions() {
    assert_eq!(
        MerkleSearchTreePolicy::<i32, i32>::ALGORITHM_ID,
        "mst-sha256-b16-v2"
    );
    for invalid in [
        "",
        " ",
        "codec",
        "-v1",
        "codec-v",
        "codec-vx",
        " codec-v1",
        "codec-v1 ",
    ] {
        let result = MerkleSearchTreePolicy::natural(
            "wire-policy-v1",
            NamedIntCodec(invalid),
            Int32MerkleCodec,
        );
        assert!(matches!(
            result,
            Err(MerklePolicyError::InvalidEncodingId { .. })
        ));
    }
    assert!(matches!(
        MerkleSearchTreePolicy::natural(" ", Int32MerkleCodec, Int32MerkleCodec),
        Err(MerklePolicyError::EmptyPolicyId)
    ));

    let first =
        MerkleSearchTreePolicy::natural("wire-policy-v1", Int32MerkleCodec, Int32MerkleCodec)
            .unwrap();
    let same =
        MerkleSearchTreePolicy::natural("wire-policy-v1", Int32MerkleCodec, Int32MerkleCodec)
            .unwrap();
    let different_policy =
        MerkleSearchTreePolicy::natural("wire-policy-v2", Int32MerkleCodec, Int32MerkleCodec)
            .unwrap();
    let different_codec = MerkleSearchTreePolicy::natural(
        "wire-policy-v1",
        NamedIntCodec("alternate-i32-v1"),
        Int32MerkleCodec,
    )
    .unwrap();
    assert!(first.is_compatible_with(&same));
    assert_ne!(first.domain_digest(), different_policy.domain_digest());
    assert_ne!(first.domain_digest(), different_codec.domain_digest());

    let mut empty_manifest = b"MST2\0".to_vec();
    empty_manifest.extend_from_slice(first.domain_digest().as_bytes());
    assert_eq!(first.empty_digest(), MerkleDigest::hash(&empty_manifest));
    assert_eq!(
        MerkleSearchTree::new(first.clone()).root_hash(),
        first.empty_digest()
    );
    assert_eq!(
        MerkleSearchTreePolicy::<i32, i32>::level(MerkleDigest::default()),
        64
    );
    let mut one_nibble = [0_u8; 32];
    one_nibble[0] = 0x01;
    assert_eq!(
        MerkleSearchTreePolicy::<i32, i32>::level(MerkleDigest::from_bytes(&one_nibble).unwrap()),
        1
    );
}

#[test]
fn mst2_single_entry_bytes_match_the_csharp_golden_vector() {
    let policy = string_policy();
    assert_eq!(
        policy.domain_digest().to_string(),
        "fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"
    );
    let tree = StringTree::new(policy)
        .set_item(42, Some("forty-two".to_owned()))
        .unwrap();
    assert_eq!(
        tree.root_hash().to_string(),
        "1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94"
    );
    let expected = decode_hex(
        "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2\
         000000000100000001000000040000002a0000000a01666f7274792d74776f\
         98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3\
         98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
            .replace(|character: char| character.is_ascii_whitespace(), "")
            .as_str(),
    );
    let blocks = tree.blocks_preorder();
    assert_eq!(blocks.len(), 1);
    assert_eq!(blocks[0].0, tree.root_hash());
    assert_eq!(blocks[0].1.as_ref(), expected);
    assert_eq!(MerkleDigest::hash(&expected), tree.root_hash());
    assert_eq!(tree.validate_structure().unwrap().count, 1);
}

#[test]
fn mst2_wide_multi_level_root_matches_every_sibling_port() {
    let policy = MerkleSearchTreePolicy::natural(
        "golden-wide-i32-i32-v1",
        Int32MerkleCodec,
        Int32MerkleCodec,
    )
    .unwrap();
    let keys = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 38, 44, 59, 464];
    let tree = MerkleSearchTree::from_entries(
        keys.into_iter().map(|key| (key, -key - 1)),
        policy.clone(),
    )
    .unwrap();
    assert_eq!(
        policy.domain_digest().to_string(),
        "eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917"
    );
    assert_eq!(
        tree.root_hash().to_string(),
        "9afd7ba98ec91f72074c5f2c272ca1334244fb43a631e0fb440e02799eee8755"
    );
    assert_eq!(tree.len(), 14);
    assert_eq!(tree.block_count(), 4);
    assert_eq!(tree.height(), 3);
    let root = &tree.blocks_preorder()[0];
    assert_eq!(root.0, tree.root_hash());
    assert_eq!(root.1[37], 2);
    assert_eq!(
        root.1.as_ref(),
        decode_hex(
            "4d53543201eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917\
             020000000e00000002000000040000003b00000004ffffffc400000004000001d000000004fffffe2f\
             790b862e0ef81c9e6debdf38c1099c565887fe87aed84f26dfba736de256d4d5\
             018b1ddc596548b5389c9523ed8ddc027d166d82540611be117f8452a685a608\
             018b1ddc596548b5389c9523ed8ddc027d166d82540611be117f8452a685a608"
                .replace(|character: char| character.is_ascii_whitespace(), "")
                .as_str(),
        )
    );
    assert_eq!(tree.validate_structure().unwrap().count, 14);
}

#[test]
fn construction_and_churn_are_history_independent() {
    let entries = (0..4_096)
        .map(|key| (key, Some(format!("value-{key}"))))
        .collect::<Vec<_>>();
    let forward = StringTree::from_entries(entries.clone(), string_policy()).unwrap();
    let reverse = StringTree::from_entries(entries.iter().cloned().rev(), string_policy()).unwrap();
    let mut incremental = StringTree::new(string_policy());
    for (key, value) in entries.iter().cloned() {
        incremental = incremental.set_item(key, value).unwrap();
    }
    assert!(forward.content_equals(&reverse));
    assert!(forward.content_equals(&incremental));
    assert_eq!(forward.shape(), reverse.shape());
    assert_eq!(forward.shape(), incremental.shape());
    assert_eq!(forward.validate_structure().unwrap().count, entries.len());
    assert!(forward.height() >= 3);
    assert!(forward.block_count() > forward.height());

    let removed = incremental.remove(&2_047);
    assert_eq!(removed.get(&2_047), None);
    let restored = removed
        .set_item(2_047, Some("value-2047".to_owned()))
        .unwrap();
    assert!(forward.content_equals(&restored));
    assert_eq!(forward.shape(), restored.shape());

    let absent = forward.remove(&-1);
    assert!(forward.shares_root_with(&absent));
    let no_op = forward
        .set_item(2_000, Some("value-2000".to_owned()))
        .unwrap();
    assert!(forward.shares_root_with(&no_op));
}

#[test]
fn updates_share_unaffected_blocks_and_old_versions_remain_valid() {
    let original = StringTree::from_entries(
        (0..8_193).map(|key| (key, Some(key.to_string()))),
        string_policy(),
    )
    .unwrap();
    let updated = original
        .set_item(4_096, Some("changed".to_owned()))
        .unwrap();

    assert_eq!(original.get(&4_096).unwrap().as_deref(), Some("4096"));
    assert_eq!(updated.get(&4_096).unwrap().as_deref(), Some("changed"));
    assert_eq!(original.len(), updated.len());
    assert_ne!(original.root_hash(), updated.root_hash());
    assert!(
        original.shared_block_count(&updated) >= original.block_count() - original.height(),
        "one value update should replace at most one root-to-entry block path"
    );
    original.validate_structure().unwrap();
    updated.validate_structure().unwrap();
}

#[test]
fn range_and_diff_follow_semantic_key_order() {
    let source = StringTree::from_entries(
        (0..100).map(|key| (key, (key % 11 != 0).then(|| key.to_string()))),
        string_policy(),
    )
    .unwrap();
    let range = source
        .enumerate_range(&23, &31)
        .unwrap()
        .map(|entry| *entry.key())
        .collect::<Vec<_>>();
    assert_eq!(range, (23..=31).collect::<Vec<_>>());
    assert!(source.enumerate_range(&5, &4).is_err());

    let target = source
        .remove(&7)
        .set_item(8, Some("eight".to_owned()))
        .unwrap()
        .set_item(1_000, None)
        .unwrap();
    let differences = source.diff(&target).unwrap();
    assert_eq!(differences.len(), 3);
    assert!(differences.iter().any(
        |difference| matches!(difference, MerkleMapDifference::Removed { key, .. } if **key == 7)
    ));
    assert!(differences.iter().any(
        |difference| matches!(difference, MerkleMapDifference::Changed { key, before, after } if **key == 8 && before.as_deref() == Some("8") && after.as_deref() == Some("eight"))
    ));
    assert!(differences.iter().any(
        |difference| matches!(difference, MerkleMapDifference::Added { key, value } if **key == 1_000 && value.is_none())
    ));
    assert!(source.diff(&source.clone()).unwrap().is_empty());

    let incompatible = StringTree::new(
        MerkleSearchTreePolicy::natural("different-v1", Int32MerkleCodec, NullableUtf8MerkleCodec)
            .unwrap(),
    );
    assert!(source.diff(&incompatible).is_err());
    assert!(!source.map_equals(&incompatible));
}

#[derive(Debug, Eq, PartialEq)]
struct RepresentativeKey {
    logical: i32,
    representative: i32,
}

#[derive(Clone, Copy)]
struct RepresentativeKeyCodec;

impl MerkleCodec<RepresentativeKey> for RepresentativeKeyCodec {
    fn encoding_id(&self) -> &str {
        "representative-key-v1"
    }

    fn encode(&self, value: &RepresentativeKey) -> Result<Vec<u8>, MerkleCodecError> {
        Int32MerkleCodec.encode(&value.logical)
    }

    fn decode(&self, encoding: &[u8]) -> Result<RepresentativeKey, MerkleCodecError> {
        Ok(RepresentativeKey {
            logical: Int32MerkleCodec.decode(encoding)?,
            representative: 0,
        })
    }
}

#[test]
fn equivalent_keys_keep_the_first_representative_and_last_value() {
    let policy = MerkleSearchTreePolicy::new(
        "representatives-v1",
        |left: &RepresentativeKey, right: &RepresentativeKey| left.logical.cmp(&right.logical),
        RepresentativeKeyCodec,
        Int32MerkleCodec,
    )
    .unwrap();
    let query = RepresentativeKey {
        logical: 5,
        representative: 99,
    };
    let tree = MerkleSearchTree::from_entries(
        [
            (
                RepresentativeKey {
                    logical: 5,
                    representative: 1,
                },
                10,
            ),
            (
                RepresentativeKey {
                    logical: 2,
                    representative: 2,
                },
                20,
            ),
            (
                RepresentativeKey {
                    logical: 5,
                    representative: 3,
                },
                30,
            ),
        ],
        policy,
    )
    .unwrap();
    let entry = tree.get_entry(&query).unwrap();
    assert_eq!(entry.key().representative, 1);
    assert_eq!(*entry.value(), 30);
    let replaced = tree
        .set_item(
            RepresentativeKey {
                logical: 5,
                representative: 4,
            },
            40,
        )
        .unwrap();
    assert_eq!(replaced.get_entry(&query).unwrap().key().representative, 1);
    assert_eq!(replaced.get(&query), Some(&40));
}

#[derive(Debug, Eq, PartialEq)]
struct NonCloneValue(i32);

#[derive(Clone, Copy)]
struct NonCloneValueCodec;

impl MerkleCodec<NonCloneValue> for NonCloneValueCodec {
    fn encoding_id(&self) -> &str {
        "non-clone-value-v1"
    }

    fn encode(&self, value: &NonCloneValue) -> Result<Vec<u8>, MerkleCodecError> {
        Int32MerkleCodec.encode(&value.0)
    }

    fn decode(&self, encoding: &[u8]) -> Result<NonCloneValue, MerkleCodecError> {
        Ok(NonCloneValue(Int32MerkleCodec.decode(encoding)?))
    }
}

#[test]
fn persistent_core_does_not_require_key_or_value_clone() {
    let policy =
        MerkleSearchTreePolicy::natural("non-clone-v1", Int32MerkleCodec, NonCloneValueCodec)
            .unwrap();
    let empty = MerkleSearchTree::new(policy);
    let one = empty.set_item(1, NonCloneValue(10)).unwrap();
    let two = one.set_item(2, NonCloneValue(20)).unwrap();
    let replaced = two.set_item(1, NonCloneValue(30)).unwrap();
    assert_eq!(empty.get(&1), None);
    assert_eq!(one.get(&1), Some(&NonCloneValue(10)));
    assert_eq!(two.get(&2), Some(&NonCloneValue(20)));
    assert_eq!(replaced.get(&1), Some(&NonCloneValue(30)));
    assert_eq!(replaced.remove(&2).get(&2), None);
}

#[test]
fn randomized_retained_versions_match_an_ordered_model() {
    let mut tree = StringTree::new(string_policy());
    let mut model = BTreeMap::new();
    let mut retained = Vec::new();
    let mut state = 0x4d59_5df4_d0f3_3173_u64;
    for step in 0..12_000 {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        let key = ((state >> 19) % 1_024) as i32 - 512;
        if state & 7 == 0 {
            tree = tree.remove(&key);
            model.remove(&key);
        } else {
            let value = (state & 15 != 0).then(|| format!("{step}:{state:016x}"));
            tree = tree.set_item(key, value.clone()).unwrap();
            model.insert(key, value);
        }
        if step % 997 == 0 {
            retained.push((tree.clone(), model.clone()));
        }
        if step % 257 == 0 {
            assert_tree_matches(&tree, &model);
        }
    }
    assert_tree_matches(&tree, &model);
    for (snapshot, expected) in retained {
        assert_tree_matches(&snapshot, &expected);
    }
}

fn assert_tree_matches(tree: &StringTree, expected: &BTreeMap<i32, Option<String>>) {
    assert_eq!(tree.len(), expected.len());
    assert_eq!(
        tree.iter()
            .map(|entry| (*entry.key(), entry.value().clone()))
            .collect::<Vec<_>>(),
        expected
            .iter()
            .map(|(key, value)| (*key, value.clone()))
            .collect::<Vec<_>>()
    );
    for (key, value) in expected {
        assert_eq!(tree.get(key), Some(value));
    }
    tree.validate_structure().unwrap();
}

#[test]
fn immutable_snapshots_support_parallel_readers() {
    fn assert_send_sync<T: Send + Sync>() {}
    assert_send_sync::<StringTree>();

    let tree = Arc::new(
        StringTree::from_entries(
            (0..2_048).map(|key| (key, Some((key * 3).to_string()))),
            string_policy(),
        )
        .unwrap(),
    );
    let root_hash = tree.root_hash();
    let handles = (0..8)
        .map(|thread_index| {
            let tree = Arc::clone(&tree);
            thread::spawn(move || {
                for pass in 0..64 {
                    let key = (thread_index * 239 + pass * 17) % 2_048;
                    assert_eq!(
                        tree.get(&key).unwrap().as_deref(),
                        Some((key * 3).to_string().as_str())
                    );
                    assert_eq!(tree.iter().count(), 2_048);
                    assert_eq!(tree.root_hash(), root_hash);
                }
            })
        })
        .collect::<Vec<_>>();
    for handle in handles {
        handle.join().unwrap();
    }
}

#[test]
fn independently_discovered_hash_layers_form_wide_canonical_blocks() {
    let policy =
        MerkleSearchTreePolicy::natural("layer-search-v1", Int32MerkleCodec, Int32MerkleCodec)
            .unwrap();
    let mut found = [
        Vec::<i32>::new(),
        Vec::new(),
        Vec::new(),
        Vec::new(),
        Vec::new(),
    ];
    for candidate in 0..1_000_000 {
        let level = MerkleSearchTreePolicy::<i32, i32>::level(policy.hash_key(&candidate).unwrap());
        let wanted = match level {
            0 if found[0].len() < 16 => true,
            1 if found[1].len() < 8 => true,
            2 if found[2].len() < 3 => true,
            3 if found[3].is_empty() => true,
            4 if found[4].is_empty() => true,
            _ => false,
        };
        if wanted {
            found[usize::from(level)].push(candidate);
        }
        if found[0].len() == 16
            && found[1].len() == 8
            && found[2].len() == 3
            && !found[3].is_empty()
            && !found[4].is_empty()
        {
            break;
        }
    }
    assert_eq!(
        found.iter().map(Vec::len).collect::<Vec<_>>(),
        [16, 8, 3, 1, 1]
    );
    let entries = found
        .iter()
        .flatten()
        .copied()
        .map(|key| (key, -key))
        .collect::<Vec<_>>();
    let forward = MerkleSearchTree::from_entries(entries.clone(), policy.clone()).unwrap();
    let reverse = MerkleSearchTree::from_entries(entries.into_iter().rev(), policy).unwrap();
    assert_eq!(forward.root_hash(), reverse.root_hash());
    assert_eq!(forward.shape(), reverse.shape());
    assert_eq!(forward.shape().first().unwrap().level, 4);
    assert!(
        forward
            .shape()
            .iter()
            .any(|entry| entry.entries_in_block > 1)
    );
    assert!(forward.height() >= 4);
    forward.validate_structure().unwrap();
}

#[test]
fn custom_value_relations_are_honored_without_changing_content_addresses() {
    let source =
        StringTree::from_entries([(1, Some("Alpha".to_owned())), (2, None)], string_policy())
            .unwrap();
    let target =
        StringTree::from_entries([(1, Some("alpha".to_owned())), (2, None)], string_policy())
            .unwrap();
    assert_ne!(source.root_hash(), target.root_hash());
    assert!(!source.map_equals(&target));
    let case_insensitive = |left: &Option<String>, right: &Option<String>| match (left, right) {
        (Some(left), Some(right)) => left.eq_ignore_ascii_case(right),
        (None, None) => true,
        _ => false,
    };
    assert!(source.map_equals_by(&target, case_insensitive));
    assert!(
        source
            .diff_by(&target, case_insensitive)
            .unwrap()
            .is_empty()
    );
}

#[test]
fn natural_policy_comparison_is_ordinary_ord() {
    let policy =
        MerkleSearchTreePolicy::natural("ordering-v1", Int32MerkleCodec, Int32MerkleCodec).unwrap();
    assert_eq!(policy.compare(&-1, &0), Ordering::Less);
    assert_eq!(policy.compare(&0, &0), Ordering::Equal);
    assert_eq!(policy.compare(&1, &0), Ordering::Greater);
}
