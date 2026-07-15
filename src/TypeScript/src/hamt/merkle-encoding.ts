import { createHash } from "node:crypto";

/** An injective, versioned canonical codec used by content-addressed structures. */
export interface MerkleCodec<T> {
  readonly encodingId: string;
  encode(value: T): Uint8Array;
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

export class MerkleDigest {
  static readonly byteLength = 32;
  readonly #bytes: Uint8Array;

  private constructor(bytes: Uint8Array) { this.#bytes = bytes.slice(); }

  static fromBytes(bytes: Uint8Array): MerkleDigest {
    exactly(bytes, 32, "SHA-256 digest");
    return new MerkleDigest(bytes);
  }

  static parse(hex: string): MerkleDigest {
    if (!/^[0-9a-f]{64}$/iu.test(hex)) throw new RangeError("A Merkle digest must contain exactly 64 hexadecimal digits.");
    return new MerkleDigest(Uint8Array.from(Buffer.from(hex, "hex")));
  }

  static hash(bytes: Uint8Array): MerkleDigest {
    return new MerkleDigest(createHash("sha256").update(bytes).digest());
  }

  toBytes(): Uint8Array { return this.#bytes.slice(); }
  toString(): string { return Buffer.from(this.#bytes).toString("hex"); }
  equals(other: MerkleDigest): boolean { return this.toString() === other.toString(); }
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

export type MerkleComparator<T> = (left: T, right: T) => number;

export class MerkleSearchTreePolicy<K, V> {
  static readonly algorithmId = "mst-sha256-b16-v2";
  readonly domainDigest: MerkleDigest;
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

  hashKey(keyBytes: Uint8Array): MerkleDigest { return framed(0x4b, [this.domainDigest.toBytes(), keyBytes]); }
  compatibleWith(other: MerkleSearchTreePolicy<K, V>): boolean { return this.domainDigest.equals(other.domainDigest); }
}

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
