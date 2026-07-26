//! Canonical byte encodings and digests underpinning the Merkle search tree.
//!
//! A content-addressed structure is only well defined if every value has exactly one byte
//! representation, so a [`MerkleCodec`] must be *injective* and *canonical*: distinct values encode
//! to distinct bytes, and a value encodes to one specific byte string rather than any acceptable
//! one. Decoding correspondingly consumes the whole slice and rejects noncanonical input —
//! wrong widths, unknown tags, trailing bytes, malformed UTF-8 — as a [`MerkleCodecError`] instead
//! of accepting it leniently. Lenient decoding here would let two peers agree on a value while
//! disagreeing on its digest.
//!
//! Each codec carries a versioned identifier ending in `-v` followed by decimal digits. That
//! identifier is mixed into the tree's digest domain, so changing an encoding changes every digest
//! derived from it rather than silently reinterpreting existing data.
//!
//! The module supplies the strict codecs for the primitive value types, [`MerkleDigest`] (exactly
//! 32 binary bytes, or 64 hexadecimal characters in text form), and the length-framed hashing
//! helpers that the tree uses to derive its domain digest and per-key digests.

use sha2::{Digest, Sha256};
use std::cmp::Ordering;
use std::error::Error;
use std::fmt;
use std::str::FromStr;
use std::sync::Arc;

/// Encodes and decodes one injective, versioned canonical value representation.
pub trait MerkleCodec<T>: Send + Sync {
    /// Returns the stable identifier ending in `-v` followed by decimal digits.
    fn encoding_id(&self) -> &str;

    /// Encodes one value into newly owned canonical bytes.
    fn encode(&self, value: &T) -> Result<Vec<u8>, MerkleCodecError>;

    /// Decodes exactly one complete canonical encoding.
    fn decode(&self, encoding: &[u8]) -> Result<T, MerkleCodecError>;
}

/// A malformed, noncanonical, or otherwise rejected codec value.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MerkleCodecError {
    message: String,
}

impl MerkleCodecError {
    /// Creates a codec error with a diagnostic message.
    #[must_use]
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }
}

impl fmt::Display for MerkleCodecError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl Error for MerkleCodecError {}

/// Canonical signed 32-bit big-endian codec (`i32-be-v1`).
#[derive(Clone, Copy, Debug, Default)]
pub struct Int32MerkleCodec;

impl MerkleCodec<i32> for Int32MerkleCodec {
    fn encoding_id(&self) -> &str {
        "i32-be-v1"
    }

    fn encode(&self, value: &i32) -> Result<Vec<u8>, MerkleCodecError> {
        Ok(value.to_be_bytes().to_vec())
    }

    fn decode(&self, encoding: &[u8]) -> Result<i32, MerkleCodecError> {
        let bytes: [u8; 4] = encoding.try_into().map_err(|_| {
            MerkleCodecError::new("invalid i32-be-v1 encoding: expected exactly four bytes")
        })?;
        Ok(i32::from_be_bytes(bytes))
    }
}

/// Canonical signed 64-bit big-endian codec (`i64-be-v1`).
#[derive(Clone, Copy, Debug, Default)]
pub struct Int64MerkleCodec;

impl MerkleCodec<i64> for Int64MerkleCodec {
    fn encoding_id(&self) -> &str {
        "i64-be-v1"
    }

    fn encode(&self, value: &i64) -> Result<Vec<u8>, MerkleCodecError> {
        Ok(value.to_be_bytes().to_vec())
    }

    fn decode(&self, encoding: &[u8]) -> Result<i64, MerkleCodecError> {
        let bytes: [u8; 8] = encoding.try_into().map_err(|_| {
            MerkleCodecError::new("invalid i64-be-v1 encoding: expected exactly eight bytes")
        })?;
        Ok(i64::from_be_bytes(bytes))
    }
}

/// Canonical nullable UTF-8 codec (`nullable-utf8-v1`).
#[derive(Clone, Copy, Debug, Default)]
pub struct NullableUtf8MerkleCodec;

impl MerkleCodec<Option<String>> for NullableUtf8MerkleCodec {
    fn encoding_id(&self) -> &str {
        "nullable-utf8-v1"
    }

    fn encode(&self, value: &Option<String>) -> Result<Vec<u8>, MerkleCodecError> {
        match value {
            None => Ok(vec![0]),
            Some(value) => {
                let mut result = Vec::with_capacity(value.len() + 1);
                result.push(1);
                result.extend_from_slice(value.as_bytes());
                Ok(result)
            }
        }
    }

    fn decode(&self, encoding: &[u8]) -> Result<Option<String>, MerkleCodecError> {
        let Some((&tag, payload)) = encoding.split_first() else {
            return Err(MerkleCodecError::new(
                "invalid nullable-utf8-v1 encoding: nullable tag is missing",
            ));
        };
        match tag {
            0 if payload.is_empty() => Ok(None),
            0 => Err(MerkleCodecError::new(
                "invalid nullable-utf8-v1 encoding: null has trailing bytes",
            )),
            1 => std::str::from_utf8(payload)
                .map(|value| Some(value.to_owned()))
                .map_err(|_| {
                    MerkleCodecError::new(
                        "invalid nullable-utf8-v1 encoding: payload is not canonical UTF-8",
                    )
                }),
            _ => Err(MerkleCodecError::new(
                "invalid nullable-utf8-v1 encoding: nullable tag must be zero or one",
            )),
        }
    }
}

/// Canonical nullable byte-vector codec (`nullable-bytes-v1`).
#[derive(Clone, Copy, Debug, Default)]
pub struct NullableBytesMerkleCodec;

impl MerkleCodec<Option<Vec<u8>>> for NullableBytesMerkleCodec {
    fn encoding_id(&self) -> &str {
        "nullable-bytes-v1"
    }

    fn encode(&self, value: &Option<Vec<u8>>) -> Result<Vec<u8>, MerkleCodecError> {
        match value {
            None => Ok(vec![0]),
            Some(value) => {
                let mut result = Vec::with_capacity(value.len() + 1);
                result.push(1);
                result.extend_from_slice(value);
                Ok(result)
            }
        }
    }

    fn decode(&self, encoding: &[u8]) -> Result<Option<Vec<u8>>, MerkleCodecError> {
        let Some((&tag, payload)) = encoding.split_first() else {
            return Err(MerkleCodecError::new(
                "invalid nullable-bytes-v1 encoding: nullable tag is missing",
            ));
        };
        match tag {
            0 if payload.is_empty() => Ok(None),
            0 => Err(MerkleCodecError::new(
                "invalid nullable-bytes-v1 encoding: null has trailing bytes",
            )),
            1 => Ok(Some(payload.to_vec())),
            _ => Err(MerkleCodecError::new(
                "invalid nullable-bytes-v1 encoding: nullable tag must be zero or one",
            )),
        }
    }
}

/// An RFC-4122/network-order 16-byte GUID value.
#[repr(transparent)]
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Rfc4122Guid([u8; 16]);

impl Rfc4122Guid {
    /// Creates a GUID from its exact RFC-4122/network-order bytes.
    #[must_use]
    pub const fn from_bytes(bytes: [u8; 16]) -> Self {
        Self(bytes)
    }

    /// Returns the exact RFC-4122/network-order bytes.
    #[must_use]
    pub const fn into_bytes(self) -> [u8; 16] {
        self.0
    }

    /// Borrows the exact RFC-4122/network-order bytes.
    #[must_use]
    pub const fn as_bytes(&self) -> &[u8; 16] {
        &self.0
    }
}

/// Canonical RFC-4122/network-order GUID codec (`guid-rfc4122-v1`).
#[derive(Clone, Copy, Debug, Default)]
pub struct Rfc4122GuidMerkleCodec;

impl MerkleCodec<Rfc4122Guid> for Rfc4122GuidMerkleCodec {
    fn encoding_id(&self) -> &str {
        "guid-rfc4122-v1"
    }

    fn encode(&self, value: &Rfc4122Guid) -> Result<Vec<u8>, MerkleCodecError> {
        Ok(value.0.to_vec())
    }

    fn decode(&self, encoding: &[u8]) -> Result<Rfc4122Guid, MerkleCodecError> {
        let bytes: [u8; 16] = encoding.try_into().map_err(|_| {
            MerkleCodecError::new(
                "invalid guid-rfc4122-v1 encoding: expected exactly sixteen bytes",
            )
        })?;
        Ok(Rfc4122Guid(bytes))
    }
}

/// A 256-bit SHA-256 content digest.
#[derive(Clone, Copy, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct MerkleDigest([u8; Self::BYTE_LENGTH]);

impl MerkleDigest {
    /// Binary digest length.
    pub const BYTE_LENGTH: usize = 32;
    /// Hexadecimal digest length.
    pub const HEX_LENGTH: usize = Self::BYTE_LENGTH * 2;

    /// Creates a digest from exactly 32 bytes.
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, MerkleDigestParseError> {
        let bytes: [u8; Self::BYTE_LENGTH] = bytes
            .try_into()
            .map_err(|_| MerkleDigestParseError::InvalidBinaryLength(bytes.len()))?;
        Ok(Self(bytes))
    }

    /// Returns the digest when `bytes` has exactly 32 bytes.
    #[must_use]
    pub fn try_from_bytes(bytes: &[u8]) -> Option<Self> {
        Self::from_bytes(bytes).ok()
    }

    /// Returns the exact digest bytes.
    #[must_use]
    pub const fn as_bytes(&self) -> &[u8; Self::BYTE_LENGTH] {
        &self.0
    }

    /// Returns newly owned digest bytes.
    #[must_use]
    pub const fn to_bytes(self) -> [u8; Self::BYTE_LENGTH] {
        self.0
    }

    /// Writes into the first 32 bytes of `destination` without changing it on failure.
    pub fn write_bytes(&self, destination: &mut [u8]) -> Result<(), MerkleDigestWriteError> {
        let Some(prefix) = destination.get_mut(..Self::BYTE_LENGTH) else {
            return Err(MerkleDigestWriteError {
                required: Self::BYTE_LENGTH,
                actual: destination.len(),
            });
        };
        prefix.copy_from_slice(&self.0);
        Ok(())
    }

    /// Parses exactly 64 hexadecimal characters, accepting either case.
    pub fn from_hex(hex: &str) -> Result<Self, MerkleDigestParseError> {
        if hex.len() != Self::HEX_LENGTH {
            return Err(MerkleDigestParseError::InvalidHexLength(hex.len()));
        }
        let mut bytes = [0_u8; Self::BYTE_LENGTH];
        for (index, destination) in bytes.iter_mut().enumerate() {
            let high = hex_value(hex.as_bytes()[index * 2])
                .ok_or(MerkleDigestParseError::InvalidHexCharacter(index * 2))?;
            let low = hex_value(hex.as_bytes()[index * 2 + 1])
                .ok_or(MerkleDigestParseError::InvalidHexCharacter(index * 2 + 1))?;
            *destination = (high << 4) | low;
        }
        Ok(Self(bytes))
    }

    /// Computes SHA-256 over `bytes`.
    #[must_use]
    pub fn hash(bytes: &[u8]) -> Self {
        let digest = Sha256::digest(bytes);
        Self(digest.into())
    }
}

impl fmt::Debug for MerkleDigest {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, formatter)
    }
}

impl fmt::Display for MerkleDigest {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        for byte in self.0 {
            write!(formatter, "{byte:02x}")?;
        }
        Ok(())
    }
}

impl FromStr for MerkleDigest {
    type Err = MerkleDigestParseError;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        Self::from_hex(value)
    }
}

/// A digest parse failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MerkleDigestParseError {
    /// Binary content had a length other than 32.
    InvalidBinaryLength(usize),
    /// Hexadecimal content had a length other than 64.
    InvalidHexLength(usize),
    /// A non-hexadecimal byte occurred at the supplied byte index.
    InvalidHexCharacter(usize),
}

impl fmt::Display for MerkleDigestParseError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidBinaryLength(actual) => write!(
                formatter,
                "a Merkle digest must contain exactly 32 bytes; received {actual}",
            ),
            Self::InvalidHexLength(actual) => write!(
                formatter,
                "a Merkle digest must contain exactly 64 hexadecimal characters; received {actual}",
            ),
            Self::InvalidHexCharacter(index) => {
                write!(
                    formatter,
                    "invalid hexadecimal character at byte index {index}"
                )
            }
        }
    }
}

impl Error for MerkleDigestParseError {}

/// A destination was too short for a digest write.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MerkleDigestWriteError {
    /// Required destination length.
    pub required: usize,
    /// Actual destination length.
    pub actual: usize,
}

impl fmt::Display for MerkleDigestWriteError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "digest destination requires {} bytes; received {}",
            self.required, self.actual,
        )
    }
}

impl Error for MerkleDigestWriteError {}

/// Compares Merkle-search-tree keys for ordering and equivalence.
pub trait MerkleKeyComparer<K>: Send + Sync {
    /// Compares two keys.
    fn compare(&self, left: &K, right: &K) -> Ordering;
}

impl<K, F> MerkleKeyComparer<K> for F
where
    F: Fn(&K, &K) -> Ordering + Send + Sync,
{
    fn compare(&self, left: &K, right: &K) -> Ordering {
        self(left, right)
    }
}

/// Natural [`Ord`] key comparison.
#[derive(Clone, Copy, Debug, Default)]
pub struct NaturalMerkleKeyComparer;

impl<K: Ord> MerkleKeyComparer<K> for NaturalMerkleKeyComparer {
    fn compare(&self, left: &K, right: &K) -> Ordering {
        left.cmp(right)
    }
}

struct PolicyInner<K, V> {
    policy_id: String,
    comparer: Arc<dyn MerkleKeyComparer<K>>,
    key_codec: Arc<dyn MerkleCodec<K>>,
    value_codec: Arc<dyn MerkleCodec<V>>,
    domain_digest: MerkleDigest,
    empty_digest: MerkleDigest,
}

/// Deterministic comparison, encoding, and SHA-256 domain for one Merkle search tree family.
pub struct MerkleSearchTreePolicy<K, V> {
    inner: Arc<PolicyInner<K, V>>,
}

impl<K, V> Clone for MerkleSearchTreePolicy<K, V> {
    fn clone(&self) -> Self {
        Self {
            inner: Arc::clone(&self.inner),
        }
    }
}

impl<K, V> fmt::Debug for MerkleSearchTreePolicy<K, V> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("MerkleSearchTreePolicy")
            .field("policy_id", &self.inner.policy_id)
            .field("algorithm_id", &Self::ALGORITHM_ID)
            .field("domain_digest", &self.inner.domain_digest)
            .finish_non_exhaustive()
    }
}

impl<K, V> MerkleSearchTreePolicy<K, V> {
    /// Exact hashing and block-format identifier.
    pub const ALGORITHM_ID: &'static str = "mst-sha256-b16-v2";

    /// Creates an explicit deterministic policy.
    pub fn new<C, KC, VC>(
        policy_id: impl Into<String>,
        comparer: C,
        key_codec: KC,
        value_codec: VC,
    ) -> Result<Self, MerklePolicyError>
    where
        C: MerkleKeyComparer<K> + 'static,
        KC: MerkleCodec<K> + 'static,
        VC: MerkleCodec<V> + 'static,
    {
        let policy_id = policy_id.into();
        if policy_id.trim().is_empty() {
            return Err(MerklePolicyError::EmptyPolicyId);
        }
        validate_encoding_id(key_codec.encoding_id(), MerklePolicyField::KeyCodec)?;
        validate_encoding_id(value_codec.encoding_id(), MerklePolicyField::ValueCodec)?;

        let domain_digest = hash_framed(
            0x50,
            &[
                Self::ALGORITHM_ID.as_bytes(),
                policy_id.as_bytes(),
                key_codec.encoding_id().as_bytes(),
                value_codec.encoding_id().as_bytes(),
            ],
        );
        let mut empty_manifest = Vec::with_capacity(5 + MerkleDigest::BYTE_LENGTH);
        empty_manifest.extend_from_slice(b"MST2");
        empty_manifest.push(0);
        empty_manifest.extend_from_slice(domain_digest.as_bytes());
        let empty_digest = MerkleDigest::hash(&empty_manifest);
        Ok(Self {
            inner: Arc::new(PolicyInner {
                policy_id,
                comparer: Arc::new(comparer),
                key_codec: Arc::new(key_codec),
                value_codec: Arc::new(value_codec),
                domain_digest,
                empty_digest,
            }),
        })
    }

    /// Creates a natural-order deterministic policy.
    pub fn natural<KC, VC>(
        policy_id: impl Into<String>,
        key_codec: KC,
        value_codec: VC,
    ) -> Result<Self, MerklePolicyError>
    where
        K: Ord,
        KC: MerkleCodec<K> + 'static,
        VC: MerkleCodec<V> + 'static,
    {
        Self::new(policy_id, NaturalMerkleKeyComparer, key_codec, value_codec)
    }

    /// Returns the application semantic/version identifier.
    #[must_use]
    pub fn policy_id(&self) -> &str {
        &self.inner.policy_id
    }

    /// Returns the policy and codec domain fingerprint.
    #[must_use]
    pub fn domain_digest(&self) -> MerkleDigest {
        self.inner.domain_digest
    }

    /// Returns the canonical empty-manifest digest.
    #[must_use]
    pub fn empty_digest(&self) -> MerkleDigest {
        self.inner.empty_digest
    }

    /// Returns the retained key codec.
    #[must_use]
    pub fn key_codec(&self) -> &dyn MerkleCodec<K> {
        self.inner.key_codec.as_ref()
    }

    /// Returns the retained value codec.
    #[must_use]
    pub fn value_codec(&self) -> &dyn MerkleCodec<V> {
        self.inner.value_codec.as_ref()
    }

    /// Compares two keys using the semantic policy.
    #[must_use]
    pub fn compare(&self, left: &K, right: &K) -> Ordering {
        self.inner.comparer.compare(left, right)
    }

    /// Returns whether two policies name the same algorithm/policy/codec domain.
    #[must_use]
    pub fn is_compatible_with(&self, other: &Self) -> bool {
        self.domain_digest() == other.domain_digest()
    }

    /// Computes the policy-bound key digest from canonical encoded bytes.
    #[must_use]
    pub fn hash_key_bytes(&self, key_bytes: &[u8]) -> MerkleDigest {
        hash_framed(0x4b, &[self.domain_digest().as_bytes(), key_bytes])
    }

    /// Encodes and hashes one key.
    pub fn hash_key(&self, key: &K) -> Result<MerkleDigest, MerkleCodecError> {
        self.key_codec()
            .encode(key)
            .map(|bytes| self.hash_key_bytes(&bytes))
    }

    /// Computes SHA-256 over exact bytes.
    #[must_use]
    pub fn hash_bytes(&self, bytes: &[u8]) -> MerkleDigest {
        MerkleDigest::hash(bytes)
    }

    /// Counts leading zero base-16 digits, yielding a level from zero through 64.
    #[must_use]
    pub fn level(digest: MerkleDigest) -> u8 {
        let mut result = 0_u8;
        for &byte in digest.as_bytes() {
            if byte & 0xf0 != 0 {
                return result;
            }
            result += 1;
            if byte & 0x0f != 0 {
                return result;
            }
            result += 1;
        }
        result
    }
}

/// Policy construction field associated with an error.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MerklePolicyField {
    /// Application policy identifier.
    PolicyId,
    /// Key codec identifier.
    KeyCodec,
    /// Value codec identifier.
    ValueCodec,
}

/// A deterministic policy could not be constructed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum MerklePolicyError {
    /// The application policy identifier was empty or whitespace.
    EmptyPolicyId,
    /// A codec identifier was empty, padded, or lacked a numeric `-v` suffix.
    InvalidEncodingId {
        /// Invalid field.
        field: MerklePolicyField,
        /// Supplied identifier.
        encoding_id: String,
    },
}

impl fmt::Display for MerklePolicyError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::EmptyPolicyId => formatter.write_str("a Merkle policy id must not be empty"),
            Self::InvalidEncodingId { field, encoding_id } => write!(
                formatter,
                "invalid {field:?} encoding id '{encoding_id}': expected an unpadded id ending in -v<digits>",
            ),
        }
    }
}

impl Error for MerklePolicyError {}

fn validate_encoding_id(
    encoding_id: &str,
    field: MerklePolicyField,
) -> Result<(), MerklePolicyError> {
    let marker = encoding_id.rfind("-v");
    let valid = !encoding_id.is_empty()
        && encoding_id.trim() == encoding_id
        && marker.is_some_and(|index| {
            index > 0
                && index + 2 < encoding_id.len()
                && encoding_id[index + 2..]
                    .bytes()
                    .all(|value| value.is_ascii_digit())
        });
    if valid {
        Ok(())
    } else {
        Err(MerklePolicyError::InvalidEncodingId {
            field,
            encoding_id: encoding_id.to_owned(),
        })
    }
}

fn hash_framed(tag: u8, fields: &[&[u8]]) -> MerkleDigest {
    let mut hash = Sha256::new();
    hash.update([tag]);
    for field in fields {
        let length = i32::try_from(field.len()).expect("Merkle framed field exceeds i32 length");
        hash.update(length.to_be_bytes());
        hash.update(field);
    }
    MerkleDigest(hash.finalize().into())
}

fn hex_value(value: u8) -> Option<u8> {
    match value {
        b'0'..=b'9' => Some(value - b'0'),
        b'a'..=b'f' => Some(value - b'a' + 10),
        b'A'..=b'F' => Some(value - b'A' + 10),
        _ => None,
    }
}
