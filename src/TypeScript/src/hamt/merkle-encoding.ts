/**
 * Canonical byte encodings and digests underpinning the Merkle search tree.
 *
 * A content-addressed structure is only well defined if every value has exactly one byte
 * representation, so a codec must be injective and canonical, and decoding rejects noncanonical
 * input rather than accepting it leniently. Each codec carries a versioned identifier that is mixed
 * into the digest domain.
 */
import { createHash } from "node:crypto";

/** An injective, versioned canonical codec used by content-addressed structures. */
export interface MerkleCodec<T> {
  /**
   * Stable identifier ending in `-v` followed by decimal digits. It is mixed into the tree's digest
   * domain, so changing an encoding changes every digest derived from it rather than silently
   * reinterpreting stored data.
   */
  readonly encodingId: string;
  /**
   * The one canonical byte representation of the value. Must be injective: distinct values encode
   * to distinct bytes, and each value has exactly one acceptable encoding.
   */
  encode(value: T): Uint8Array;
  /**
   * The value encoded by these bytes, consuming the whole slice. Must reject noncanonical input
   * rather than accepting it leniently, since lenient decoding would let two peers agree on a value
   * while disagreeing on its digest.
   */
  decode(bytes: Uint8Array): T;
}

const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });

function rejectUnpairedSurrogates(value: string): void {
  for (let index = 0; index < value.length; index++) {
    const code = value.charCodeAt(index);
    if (code >= 0xd800 && code <= 0xdbff) {
      const next = value.charCodeAt(++index);
      if (!(next >= 0xdc00 && next <= 0xdfff)) throw new TypeError("String contains an unpaired UTF-16 surrogate.");
    } else if (code >= 0xdc00 && code <= 0xdfff) {
      throw new TypeError("String contains an unpaired UTF-16 surrogate.");
    }
  }
}

function strictUtf8(value: string): Uint8Array {
  rejectUnpairedSurrogates(value);
  return encoder.encode(value);
}

function exactly(bytes: Uint8Array, length: number, id: string): void {
  if (bytes.length !== length) throw new RangeError(`Invalid ${id} encoding: expected exactly ${length} bytes.`);
}

/** Codec encoding a number as exactly four big-endian two's-complement bytes. */
export const Int32MerkleCodec: MerkleCodec<number> = {
  encodingId: "i32-be-v1",
  encode(value) {
    if (!Number.isInteger(value) || value < -0x8000_0000 || value > 0x7fff_ffff) throw new RangeError("Value is not an int32.");
    const result = new Uint8Array(4);
    new DataView(result.buffer).setInt32(0, value, false);
    return result;
  },
  decode(bytes) {
    exactly(bytes, 4, this.encodingId);
    return new DataView(bytes.buffer, bytes.byteOffset, 4).getInt32(0, false);
  },
};

/** Codec encoding a bigint as exactly eight big-endian two's-complement bytes. */
export const Int64MerkleCodec: MerkleCodec<bigint> = {
  encodingId: "i64-be-v1",
  encode(value) {
    if (value < -(1n << 63n) || value >= (1n << 63n)) throw new RangeError("Value is not an int64.");
    const result = new Uint8Array(8);
    new DataView(result.buffer).setBigInt64(0, value, false);
    return result;
  },
  decode(bytes) {
    exactly(bytes, 8, this.encodingId);
    return new DataView(bytes.buffer, bytes.byteOffset, 8).getBigInt64(0, false);
  },
};

/**
 * Codec encoding `undefined` as a lone tag byte and a string as a tag followed by strict UTF-8, so
 * absence stays distinct from the empty string.
 */
export const NullableUtf8MerkleCodec: MerkleCodec<string | null> = {
  encodingId: "nullable-utf8-v1",
  encode(value) {
    if (value === null) return Uint8Array.of(0);
    const payload = strictUtf8(value);
    const result = new Uint8Array(payload.length + 1);
    result[0] = 1;
    result.set(payload, 1);
    return result;
  },
  decode(bytes) {
    if (bytes.length === 0) throw new RangeError("Missing nullable UTF-8 tag.");
    if (bytes[0] === 0) {
      exactly(bytes, 1, this.encodingId);
      return null;
    }
    if (bytes[0] !== 1) throw new RangeError("Invalid nullable UTF-8 tag.");
    return decoder.decode(bytes.subarray(1));
  },
};

/** Codec encoding `undefined` as a lone tag byte and bytes as a tag followed by the payload. */
export const NullableBytesMerkleCodec: MerkleCodec<Uint8Array | null> = {
  encodingId: "nullable-bytes-v1",
  encode(value) {
    if (value === null) return Uint8Array.of(0);
    const result = new Uint8Array(value.length + 1);
    result[0] = 1;
    result.set(value, 1);
    return result;
  },
  decode(bytes) {
    if (bytes.length === 0) throw new RangeError("Missing nullable byte-array tag.");
    if (bytes[0] === 0) {
      exactly(bytes, 1, this.encodingId);
      return null;
    }
    if (bytes[0] !== 1) throw new RangeError("Invalid nullable byte-array tag.");
    return bytes.slice(1);
  },
};

/** UUID values use the canonical lower-case 8-4-4-4-12 textual form at the API boundary. */
export const Rfc4122UuidMerkleCodec: MerkleCodec<string> = {
  encodingId: "guid-rfc4122-v1",
  encode(value) {
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/iu.test(value)) throw new RangeError("Invalid RFC-4122 UUID.");
    return Uint8Array.from(Buffer.from(value.replaceAll("-", ""), "hex"));
  },
  decode(bytes) {
    exactly(bytes, 16, this.encodingId);
    const hex = Buffer.from(bytes).toString("hex");
    return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
  },
};

/** An immutable 32-byte SHA-256 content address. */
export class MerkleDigest {
  /** The fixed digest width in bytes. */
  static readonly byteLength = 32;
  readonly #bytes: Uint8Array;

  private constructor(bytes: Uint8Array) { this.#bytes = bytes.slice(); }

  /** The digest holding exactly these 32 bytes. */
  static fromBytes(bytes: Uint8Array): MerkleDigest {
    exactly(bytes, 32, "SHA-256 digest");
    return new MerkleDigest(bytes);
  }

  /**
   * The digest denoted by exactly 64 hexadecimal digits. Any other input is rejected; there is no
   * lenient or partial parse.
   */
  static parse(hex: string): MerkleDigest {
    if (!/^[0-9a-f]{64}$/iu.test(hex)) throw new RangeError("A Merkle digest must contain exactly 64 hexadecimal digits.");
    return new MerkleDigest(Uint8Array.from(Buffer.from(hex, "hex")));
  }

  /** The SHA-256 digest of the given bytes. */
  static hash(bytes: Uint8Array): MerkleDigest {
    return new MerkleDigest(createHash("sha256").update(bytes).digest());
  }

  /** The 32 raw digest bytes. */
  toBytes(): Uint8Array { return this.#bytes.slice(); }
  /** The digest as 64 lowercase hexadecimal digits. */
  toString(): string { return Buffer.from(this.#bytes).toString("hex"); }
  /** Whether both digests hold the same bytes. */
  equals(other: MerkleDigest): boolean { return this.toString() === other.toString(); }
  /** Order two digests lexicographically by their bytes. */
  compareTo(other: MerkleDigest): number { return Buffer.compare(this.#bytes, other.#bytes); }
}

function i32(value: number): Uint8Array {
  const result = new Uint8Array(4);
  new DataView(result.buffer).setInt32(0, value, false);
  return result;
}

function concat(parts: readonly Uint8Array[]): Uint8Array {
  const result = new Uint8Array(parts.reduce((sum, part) => sum + part.length, 0));
  let offset = 0;
  for (const part of parts) { result.set(part, offset); offset += part.length; }
  return result;
}

function framed(tag: number, fields: readonly Uint8Array[]): MerkleDigest {
  return MerkleDigest.hash(concat([Uint8Array.of(tag), ...fields.flatMap(field => [i32(field.length), field])]));
}

/**
 * Three-way key comparison: negative, zero, or positive as the left key orders before, with, or
 * after the right. Returning zero places both keys in the same equivalence class.
 */
export type MerkleComparator<T> = (left: T, right: T) => number;

/**
 * The comparator, codecs, and derived digest domain that fix one Merkle map's wire format. Trees
 * built under different policies are never interchangeable, and the domain digest is what makes
 * that checkable.
 */
export class MerkleSearchTreePolicy<K, V> {
  /** The algorithm identifier bound into the digest domain. */
  static readonly algorithmId = "mst-sha256-b16-v2";
  /** The digest binding the algorithm, policy, and both codec identifiers together. */
  readonly domainDigest: MerkleDigest;
  /** The canonical root digest of an empty tree under this policy. */
  readonly emptyDigest: MerkleDigest;

  constructor(
    readonly policyId: string,
    readonly comparer: MerkleComparator<K>,
    readonly keyCodec: MerkleCodec<K>,
    readonly valueCodec: MerkleCodec<V>,
  ) {
    if (policyId.trim().length === 0) throw new RangeError("A policy ID must be nonblank.");
    for (const id of [keyCodec.encodingId, valueCodec.encodingId]) {
      if (!/^[^\s].*-v[0-9]+$/u.test(id) || /\s$/u.test(id)) throw new RangeError(`Invalid versioned codec ID: ${id}`);
    }
    const fields = [MerkleSearchTreePolicy.algorithmId, policyId, keyCodec.encodingId, valueCodec.encodingId].map(strictUtf8);
    this.domainDigest = framed(0x50, fields);
    this.emptyDigest = MerkleDigest.hash(concat([strictUtf8("MST2"), Uint8Array.of(0), this.domainDigest.toBytes()]));
  }

  /**
   * The policy-bound digest of an encoded key. Binding the domain into the key hash is what makes
   * entry levels, and therefore tree shape, specific to this policy.
   */
  hashKey(keyBytes: Uint8Array): MerkleDigest { return framed(0x4b, [this.domainDigest.toBytes(), keyBytes]); }
  /** Whether both policies derive the same digest domain, and so describe the same wire format. */
  compatibleWith(other: MerkleSearchTreePolicy<K, V>): boolean { return this.domainDigest.equals(other.domainDigest); }
}

/** The built-in strict codecs, keyed by value type. */
export const MerkleCodecs: {
  readonly int32: MerkleCodec<number>;
  readonly int64: MerkleCodec<bigint>;
  readonly utf8String: MerkleCodec<string | null>;
  readonly bytes: MerkleCodec<Uint8Array | null>;
  readonly uuid: MerkleCodec<string>;
} = {
  int32: Int32MerkleCodec,
  int64: Int64MerkleCodec,
  utf8String: NullableUtf8MerkleCodec,
  bytes: NullableBytesMerkleCodec,
  uuid: Rfc4122UuidMerkleCodec,
} as const;

/** @internal */
export const MerkleEncodingInternals: {
  readonly concat: (parts: readonly Uint8Array[]) => Uint8Array;
  readonly i32: (value: number) => Uint8Array;
  readonly strictUtf8: (value: string) => Uint8Array;
} = { concat, i32, strictUtf8 };
