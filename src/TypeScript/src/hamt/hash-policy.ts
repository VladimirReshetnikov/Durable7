/** Runtime hashing and equivalence policy used by the persistent hash collections. */
export interface HashPolicy<K> {
    hash(key: K): number;
    equivalent(left: K, right: K): boolean;
}

const objectHashes = new WeakMap<object, number>();
const symbolHashes = new Map<symbol, number>();
let nextIdentityHash = 1;

function mix32(value: number): number {
    value = Math.imul(value ^ (value >>> 16), 0x7feb352d);
    value = Math.imul(value ^ (value >>> 15), 0x846ca68b);
    return (value ^ (value >>> 16)) | 0;
}

function hashString(value: string): number {
    let hash = 0x811c9dc5;
    for (let index = 0; index < value.length; index++) {
        hash = Math.imul(hash ^ value.charCodeAt(index), 0x01000193);
    }
    return hash | 0;
}

function identityHash(value: object): number {
    const known = objectHashes.get(value);
    if (known !== undefined) return known;
    const created = mix32(nextIdentityHash++);
    objectHashes.set(value, created);
    return created;
}

function symbolHash(value: symbol): number {
    const known = symbolHashes.get(value);
    if (known !== undefined) return known;
    const created = mix32(nextIdentityHash++);
    symbolHashes.set(value, created);
    return created;
}

/** SameValueZero equality, matching JavaScript Map and Set key semantics. */
export function sameValueZero<T>(left: T, right: T): boolean {
    return left === right || (left !== left && right !== right);
}

/** Stable process-local 32-bit hash for JavaScript primitive and object keys. */
export function defaultHash(value: unknown): number {
    switch (typeof value) {
        case "undefined": return 0x42108421;
        case "boolean": return value ? 0x1231 : 0x1237;
        case "number": {
            if (Number.isNaN(value)) return 0x7fc00000;
            if (value === 0) return 0;
            if (Number.isSafeInteger(value)) return mix32(value | 0) ^ mix32(Math.trunc(value / 0x1_0000_0000));
            const buffer = new ArrayBuffer(8);
            const view = new DataView(buffer);
            view.setFloat64(0, value, true);
            return mix32(view.getInt32(0, true) ^ view.getInt32(4, true));
        }
        case "bigint": {
            let remaining = value < 0n ? -value : value;
            let hash = value < 0n ? -1 : 0;
            while (remaining !== 0n) {
                hash = mix32(hash ^ Number(remaining & 0xffff_ffffn));
                remaining >>= 32n;
            }
            return hash;
        }
        case "string": return hashString(value);
        case "symbol": return symbolHash(value);
        case "function":
        case "object": return value === null ? 0 : identityHash(value);
    }
}

const defaultPolicy: HashPolicy<unknown> = {
    hash: defaultHash,
    equivalent: sameValueZero,
};

/** Returns the shared default SameValueZero/identity-aware hash policy. */
export function defaultHashPolicy<K>(): HashPolicy<K> {
    return defaultPolicy as HashPolicy<K>;
}

/** Creates a policy from explicit hash and equivalence functions. */
export function createHashPolicy<K>(
    hash: (key: K) => number,
    equivalent: (left: K, right: K) => boolean,
): HashPolicy<K> {
    return { hash: (key: K): number => hash(key) | 0, equivalent };
}
