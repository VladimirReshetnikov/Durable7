"""Canonical codecs and policy framing for the exact ``mst-sha256-b16-v2`` wire."""

from __future__ import annotations

import hashlib
import re
import struct
import uuid
from dataclasses import dataclass, field
from typing import ClassVar, Final, Generic, Protocol, TypeVar

T = TypeVar("T")
K = TypeVar("K")
V = TypeVar("V")
K_contra = TypeVar("K_contra", contravariant=True)


class MerkleCodec(Protocol[T]):
    """Injective, versioned canonical encoding used by authenticated structures."""

    @property
    def encoding_id(self) -> str:
        """Stable identifier ending in ``-v`` followed by decimal digits.

        Mixed into the tree's digest domain, so changing an encoding changes every digest derived
        from it rather than silently reinterpreting stored data.
        """
        ...

    def encode(self, value: T) -> bytes:
        """Return the one canonical byte representation of ``value``.

        Must be injective: distinct values encode to distinct bytes, and each value has exactly
        one acceptable encoding.
        """
        ...

    def decode(self, encoding: bytes) -> T:
        """Return the value encoded by ``encoding``, consuming the whole slice.

        Must reject noncanonical input - wrong widths, unknown tags, trailing bytes - rather than
        accepting it leniently, since lenient decoding would let two peers agree on a value while
        disagreeing on its digest.
        """
        ...


class MerkleComparator(Protocol[K_contra]):
    """Three-way key comparison policy."""

    def __call__(self, left: K_contra, right: K_contra) -> int:
        """Return a negative number, zero, or a positive number as ``left`` orders before, with,
        or after ``right``.

        Returning zero places both keys in the same equivalence class.
        """
        ...


def _strict_utf8(value: str) -> bytes:
    # Python's strict UTF-8 encoder rejects lone surrogate code points.
    return value.encode("utf-8", errors="strict")


def _i32(value: int) -> bytes:
    if not -(1 << 31) <= value < (1 << 31):
        raise OverflowError("MST2 signed 32-bit field overflow")
    return struct.pack(">i", value)


def _framed(tag: int, fields: tuple[bytes, ...]) -> MerkleDigest:
    return MerkleDigest.hash(bytes((tag,)) + b"".join(_i32(len(part)) + part for part in fields))


@dataclass(frozen=True, slots=True, order=True)
class MerkleDigest:
    """An immutable 32-byte SHA-256 content address."""

    _bytes: bytes = field(repr=False)

    BYTE_LENGTH: ClassVar[int] = 32

    def __post_init__(self) -> None:
        """Normalize the payload to ``bytes``, rejecting anything not exactly 32 bytes long."""

        value = bytes(self._bytes)
        if len(value) != self.BYTE_LENGTH:
            raise ValueError("a Merkle digest must contain exactly 32 bytes")
        object.__setattr__(self, "_bytes", value)

    @classmethod
    def from_bytes(cls, value: bytes | bytearray | memoryview) -> MerkleDigest:
        """Return the digest holding exactly these 32 bytes."""

        return cls(bytes(value))

    @classmethod
    def parse(cls, value: str) -> MerkleDigest:
        """Return the digest denoted by exactly 64 hexadecimal digits, in either case.

        Raises :class:`ValueError` on any other input; there is no lenient or partial parse.
        """

        if re.fullmatch(r"[0-9a-fA-F]{64}", value) is None:
            raise ValueError("a Merkle digest must contain exactly 64 hexadecimal digits")
        return cls(bytes.fromhex(value))

    @classmethod
    def hash(cls, value: bytes | bytearray | memoryview) -> MerkleDigest:
        """Return the SHA-256 digest of ``value``."""

        return cls(hashlib.sha256(value).digest())

    def to_bytes(self) -> bytes:
        """Return the 32 raw digest bytes."""

        return self._bytes

    def __bytes__(self) -> bytes:
        """Return the 32 raw digest bytes, so ``bytes(digest)`` works directly."""

        return self._bytes

    def __str__(self) -> str:
        """Return the digest as 64 lowercase hexadecimal digits."""

        return self._bytes.hex()


class _Int32MerkleCodec:
    encoding_id = "i32-be-v1"

    def encode(self, value: int) -> bytes:
        """Encode as exactly four big-endian two's-complement bytes. ``bool`` is rejected even
        though it subclasses ``int``, so ``True`` cannot silently encode as ``1``.
        """

        if isinstance(value, bool) or not isinstance(value, int):
            raise TypeError("int32 codec requires an int")
        if not -(1 << 31) <= value < (1 << 31):
            raise OverflowError("value is outside signed int32 range")
        return struct.pack(">i", value)

    def decode(self, encoding: bytes) -> int:
        """Decode exactly four big-endian two's-complement bytes."""

        if len(encoding) != 4:
            raise ValueError("i32-be-v1 requires exactly four bytes")
        return int.from_bytes(encoding, "big", signed=True)


class _Int64MerkleCodec:
    encoding_id = "i64-be-v1"

    def encode(self, value: int) -> bytes:
        """Encode as exactly eight big-endian two's-complement bytes."""

        if isinstance(value, bool) or not isinstance(value, int):
            raise TypeError("int64 codec requires an int")
        if not -(1 << 63) <= value < (1 << 63):
            raise OverflowError("value is outside signed int64 range")
        return struct.pack(">q", value)

    def decode(self, encoding: bytes) -> int:
        """Decode exactly eight big-endian two's-complement bytes."""

        if len(encoding) != 8:
            raise ValueError("i64-be-v1 requires exactly eight bytes")
        return int.from_bytes(encoding, "big", signed=True)


class _NullableUtf8MerkleCodec:
    encoding_id = "nullable-utf8-v1"

    def encode(self, value: str | None) -> bytes:
        """Encode ``None`` as the single tag byte 0, and a string as tag 1 followed by strict UTF-8.

        The tag is what keeps ``None`` distinguishable from the empty string.
        """

        if value is None:
            return b"\x00"
        if not isinstance(value, str):
            raise TypeError("nullable UTF-8 codec requires str or None")
        return b"\x01" + _strict_utf8(value)

    def decode(self, encoding: bytes) -> str | None:
        """Decode the nullable string form, rejecting unknown tags and malformed UTF-8."""

        if not encoding:
            raise ValueError("missing nullable UTF-8 tag")
        if encoding[0] == 0:
            if len(encoding) != 1:
                raise ValueError("null UTF-8 encoding has trailing bytes")
            return None
        if encoding[0] != 1:
            raise ValueError("invalid nullable UTF-8 tag")
        return encoding[1:].decode("utf-8", errors="strict")


class _NullableBytesMerkleCodec:
    encoding_id = "nullable-bytes-v1"

    def encode(self, value: bytes | None) -> bytes:
        """Encode ``None`` as the single tag byte 0, and a byte string as tag 1 followed by its
        bytes.
        """

        if value is None:
            return b"\x00"
        if not isinstance(value, bytes):
            raise TypeError("nullable byte codec requires immutable bytes or None")
        return b"\x01" + value

    def decode(self, encoding: bytes) -> bytes | None:
        """Decode the nullable byte-string form, rejecting unknown tags."""

        if not encoding:
            raise ValueError("missing nullable byte tag")
        if encoding[0] == 0:
            if len(encoding) != 1:
                raise ValueError("null byte encoding has trailing bytes")
            return None
        if encoding[0] != 1:
            raise ValueError("invalid nullable byte tag")
        return bytes(encoding[1:])


class _Rfc4122UuidMerkleCodec:
    encoding_id = "guid-rfc4122-v1"

    def encode(self, value: uuid.UUID) -> bytes:
        """Encode as exactly 16 RFC 4122 network-order bytes."""

        if not isinstance(value, uuid.UUID):
            raise TypeError("RFC-4122 UUID codec requires uuid.UUID")
        return value.bytes

    def decode(self, encoding: bytes) -> uuid.UUID:
        """Decode exactly 16 RFC 4122 network-order bytes."""

        if len(encoding) != 16:
            raise ValueError("guid-rfc4122-v1 requires exactly sixteen bytes")
        return uuid.UUID(bytes=encoding)


INT32_MERKLE_CODEC: Final[MerkleCodec[int]] = _Int32MerkleCodec()
INT64_MERKLE_CODEC: Final[MerkleCodec[int]] = _Int64MerkleCodec()
NULLABLE_UTF8_MERKLE_CODEC: Final[MerkleCodec[str | None]] = _NullableUtf8MerkleCodec()
NULLABLE_BYTES_MERKLE_CODEC: Final[MerkleCodec[bytes | None]] = _NullableBytesMerkleCodec()
RFC4122_UUID_MERKLE_CODEC: Final[MerkleCodec[uuid.UUID]] = _Rfc4122UuidMerkleCodec()

# Readable aliases matching the sibling-workspace type names.
Int32MerkleCodec = INT32_MERKLE_CODEC
Int64MerkleCodec = INT64_MERKLE_CODEC
NullableUtf8MerkleCodec = NULLABLE_UTF8_MERKLE_CODEC
NullableBytesMerkleCodec = NULLABLE_BYTES_MERKLE_CODEC
Rfc4122UuidMerkleCodec = RFC4122_UUID_MERKLE_CODEC

_ENCODING_ID = re.compile(r"^\S.*-v[0-9]+$", flags=re.UNICODE)


@dataclass(frozen=True, slots=True)
class MerkleSearchTreePolicy(Generic[K, V]):
    """Comparator, codecs, and exact domain separation for one Merkle map policy."""

    policy_id: str
    comparer: MerkleComparator[K]
    key_codec: MerkleCodec[K]
    value_codec: MerkleCodec[V]
    domain_digest: MerkleDigest = field(init=False)
    empty_digest: MerkleDigest = field(init=False)

    ALGORITHM_ID: ClassVar[str] = "mst-sha256-b16-v2"

    def __post_init__(self) -> None:
        """Validate the policy and codec identifiers, then derive the domain and empty-root digests.

        The domain digest binds the algorithm ID, the policy ID, and both codec IDs, so trees built
        under different policies cannot be confused for one another.
        """

        if not isinstance(self.policy_id, str) or not self.policy_id or self.policy_id.isspace():
            raise ValueError("a Merkle policy ID must not be blank")
        ids = (self.key_codec.encoding_id, self.value_codec.encoding_id)
        for encoding_id in ids:
            if _ENCODING_ID.fullmatch(encoding_id) is None or encoding_id[-1].isspace():
                raise ValueError(f"invalid versioned codec ID: {encoding_id!r}")
        fields = tuple(_strict_utf8(value) for value in (self.ALGORITHM_ID, self.policy_id, *ids))
        domain = _framed(0x50, fields)
        empty = MerkleDigest.hash(b"MST2\x00" + bytes(domain))
        object.__setattr__(self, "domain_digest", domain)
        object.__setattr__(self, "empty_digest", empty)

    def hash_key(self, key_encoding: bytes) -> MerkleDigest:
        """Return the policy-bound digest of an encoded key.

        Binding the domain digest into the key hash is what makes entry levels - and therefore tree
        shape - specific to this policy.
        """

        return _framed(0x4B, (bytes(self.domain_digest), bytes(key_encoding)))

    def is_compatible_with(self, other: MerkleSearchTreePolicy[object, object]) -> bool:
        """Whether both policies derive the same digest domain, and so describe the same wire
        format.

        Compares the derived domain digest rather than object identity, so independently constructed
        but identically configured policies are compatible.
        """

        return self.domain_digest == other.domain_digest


def merkle_level(digest: MerkleDigest) -> int:
    """Return the number of leading zero nibbles in a SHA-256 digest (0 through 64)."""

    level = 0
    for value in bytes(digest):
        if value >> 4:
            return level
        level += 1
        if value & 0x0F:
            return level
        level += 1
    return level


__all__ = [
    "INT32_MERKLE_CODEC",
    "INT64_MERKLE_CODEC",
    "NULLABLE_BYTES_MERKLE_CODEC",
    "NULLABLE_UTF8_MERKLE_CODEC",
    "RFC4122_UUID_MERKLE_CODEC",
    "Int32MerkleCodec",
    "Int64MerkleCodec",
    "MerkleCodec",
    "MerkleComparator",
    "MerkleDigest",
    "MerkleSearchTreePolicy",
    "NullableBytesMerkleCodec",
    "NullableUtf8MerkleCodec",
    "Rfc4122UuidMerkleCodec",
]
