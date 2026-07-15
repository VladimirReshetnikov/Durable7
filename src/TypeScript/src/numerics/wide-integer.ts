export type ByteOrder = "little-endian" | "big-endian";

function modulus(width: number): bigint { return 1n << BigInt(width); }
function normalizeBits(value: bigint, width: number): bigint { const mod = modulus(width); const result = value % mod; return result < 0n ? result + mod : result; }

/** Shared method surface for fixed-width integer values. */
export abstract class FixedWidthInteger<TSelf extends FixedWidthInteger<TSelf>> {
    readonly #bits: bigint;
    public abstract readonly width: number;
    public abstract readonly signed: boolean;
    protected constructor(bits: bigint) { this.#bits = bits; }
    protected abstract create(value: bigint): TSelf;
    protected normalizedBits(): bigint { return normalizeBits(this.#bits, this.width); }
    public toBigInt(): bigint {
        const bits = this.normalizedBits();
        if (!this.signed) return bits;
        const sign = 1n << BigInt(this.width - 1);
        return (bits & sign) === 0n ? bits : bits - modulus(this.width);
    }
    public toUnsignedBigInt(): bigint { return this.normalizedBits(); }
    public equals(other: TSelf): boolean { return this.normalizedBits() === other.normalizedBits(); }
    public get isZero(): boolean { return this.normalizedBits() === 0n; }
    public get isNegative(): boolean { return this.signed && this.toBigInt() < 0n; }
    public get isEven(): boolean { return (this.normalizedBits() & 1n) === 0n; }
    public get isOdd(): boolean { return !this.isEven; }
    public get isPowerOfTwo(): boolean { const value = this.toBigInt(); return value > 0n && (value & (value - 1n)) === 0n; }
    public compareTo(other: TSelf): number { const left = this.toBigInt(); const right = other.toBigInt(); return left < right ? -1 : left > right ? 1 : 0; }
    public add(other: TSelf): TSelf { return this.create(this.toBigInt() + other.toBigInt()); }
    public subtract(other: TSelf): TSelf { return this.create(this.toBigInt() - other.toBigInt()); }
    public multiply(other: TSelf): TSelf { return this.create(this.toBigInt() * other.toBigInt()); }
    public divide(other: TSelf): TSelf { if (other.toBigInt() === 0n) throw new RangeError("Division by zero."); if (this.signed && this.toBigInt() === -(1n << BigInt(this.width - 1)) && other.toBigInt() === -1n) throw new RangeError("Signed division overflow."); return this.create(this.toBigInt() / other.toBigInt()); }
    public remainder(other: TSelf): TSelf { if (other.toBigInt() === 0n) throw new RangeError("Division by zero."); if (this.signed && this.toBigInt() === -(1n << BigInt(this.width - 1)) && other.toBigInt() === -1n) throw new RangeError("Signed remainder overflow."); return this.create(this.toBigInt() % other.toBigInt()); }
    public divRem(other: TSelf): { readonly quotient: TSelf; readonly remainder: TSelf } { return { quotient: this.divide(other), remainder: this.remainder(other) }; }
    public negate(): TSelf { return this.create(-this.toBigInt()); }
    public increment(): TSelf { return this.create(this.toBigInt() + 1n); }
    public decrement(): TSelf { return this.create(this.toBigInt() - 1n); }
    public abs(): TSelf { const value = this.toBigInt(); if (this.signed && value === -(1n << BigInt(this.width - 1))) throw new RangeError("Absolute value overflow."); return this.create(value < 0n ? -value : value); }
    public bitwiseAnd(other: TSelf): TSelf { return this.create(this.normalizedBits() & other.normalizedBits()); }
    public bitwiseOr(other: TSelf): TSelf { return this.create(this.normalizedBits() | other.normalizedBits()); }
    public bitwiseXor(other: TSelf): TSelf { return this.create(this.normalizedBits() ^ other.normalizedBits()); }
    public bitwiseNot(): TSelf { return this.create(~this.normalizedBits()); }
    public shiftLeft(count: number): TSelf { const shift = BigInt(((count % this.width) + this.width) % this.width); return this.create(this.normalizedBits() << shift); }
    public shiftRight(count: number): TSelf { const shift = BigInt(((count % this.width) + this.width) % this.width); return this.create(this.signed ? this.toBigInt() >> shift : this.normalizedBits() >> shift); }
    public rotateLeft(count: number): TSelf { const normalized = ((count % this.width) + this.width) % this.width; if (normalized === 0) return this.create(this.normalizedBits()); const shift = BigInt(normalized); return this.create((this.normalizedBits() << shift) | (this.normalizedBits() >> BigInt(this.width - normalized))); }
    public rotateRight(count: number): TSelf { return this.rotateLeft(-count); }
    public checkedAdd(other: TSelf): TSelf { return this.checked(this.toBigInt() + other.toBigInt()); }
    public checkedSubtract(other: TSelf): TSelf { return this.checked(this.toBigInt() - other.toBigInt()); }
    public checkedMultiply(other: TSelf): TSelf { return this.checked(this.toBigInt() * other.toBigInt()); }
    public checkedNegate(): TSelf { return this.checked(-this.toBigInt()); }
    public checked(value: bigint): TSelf { const minimum = this.signed ? -(1n << BigInt(this.width - 1)) : 0n; const maximum = this.signed ? (1n << BigInt(this.width - 1)) - 1n : modulus(this.width) - 1n; if (value < minimum || value > maximum) throw new RangeError(`Value does not fit in ${this.width} bits.`); return this.create(value); }
    public getShortestBitLength(): number { const value = this.toBigInt(); if (value === 0n) return 0; if (!this.signed) return value.toString(2).length; if (value > 0n) return value.toString(2).length + 1; return (~value).toString(2).length + 1; }
    public leadingZeroCount(): number { const bits = this.normalizedBits(); return bits === 0n ? this.width : this.width - bits.toString(2).length; }
    public trailingZeroCount(): number { let bits = this.normalizedBits(); if (bits === 0n) return this.width; let count = 0; while ((bits & 1n) === 0n) { bits >>= 1n; count++; } return count; }
    public popCount(): number { let bits = this.normalizedBits(), count = 0; while (bits !== 0n) { bits &= bits - 1n; count++; } return count; }
    public log2(): number { const value = this.toBigInt(); if (value < 0n) throw new RangeError("Log2 is undefined for negative values."); return value === 0n ? 0 : value.toString(2).length - 1; }
    public getByteCount(): number { return this.width / 8; }
    public toBytes(order: ByteOrder = "little-endian"): Uint8Array {
        const bytes = new Uint8Array(this.getByteCount()); let value = this.normalizedBits();
        for (let index = 0; index < bytes.length; index++) { const target = order === "little-endian" ? index : bytes.length - index - 1; bytes[target] = Number(value & 0xffn); value >>= 8n; }
        return bytes;
    }
    public toString(radix = 10): string { if (radix < 2 || radix > 36) throw new RangeError("Radix must be between 2 and 36."); return (radix === 16 ? this.normalizedBits() : this.toBigInt()).toString(radix); }
    public format(format = "G"): string {
        const match = /^([gGdDnNxX])([0-9]*)$/.exec(format); if (match === null) throw new RangeError("Unsupported integer format.");
        const code = match[1]!; const precision = match[2] === "" ? (code === "N" || code === "n" ? 2 : 0) : Number(match[2]);
        if ((code === "G" || code === "g") && precision !== 0) throw new RangeError("General integer format does not accept precision.");
        if (code === "X" || code === "x") { const text = this.normalizedBits().toString(16).padStart(precision, "0"); return code === "X" ? text.toUpperCase() : text; }
        const negative = this.toBigInt() < 0n; const digits = (negative ? -this.toBigInt() : this.toBigInt()).toString().padStart(precision, "0");
        if (code === "N" || code === "n") { const grouped = digits.replace(/\B(?=(\d{3})+(?!\d))/g, ","); return `${negative ? "-" : ""}${grouped}${precision === 0 ? "" : `.${"0".repeat(precision)}`}`; }
        return `${negative ? "-" : ""}${digits}`;
    }
}

function parseInteger(text: string, radix: number): bigint {
    const trimmed = text.trim(); if (trimmed.length === 0) throw new SyntaxError("The input is not an integer.");
    if (radix === 10) { if (!/^[+-]?\d+$/.test(trimmed)) throw new SyntaxError("The input is not a decimal integer."); return BigInt(trimmed); }
    const negative = trimmed.startsWith("-"); const unsigned = /^[+-]/.test(trimmed) ? trimmed.slice(1) : trimmed;
    const digits = radix === 16 ? /^[0-9a-f]+$/i : radix === 2 ? /^[01]+$/ : undefined;
    if (digits === undefined || !digits.test(unsigned)) throw new SyntaxError("The input is not valid for the requested radix.");
    const value = BigInt(`${radix === 16 ? "0x" : "0b"}${unsigned}`); return negative ? -value : value;
}

function parseFixed(text: string, radix: number, width: number, signed: boolean): bigint {
    const value = parseInteger(text, radix);
    const explicitNegative = text.trim().startsWith("-");
    const minimum = signed ? -(1n << BigInt(width - 1)) : 0n;
    const maximum = signed && (radix === 10 || explicitNegative) ? (1n << BigInt(width - 1)) - 1n : (1n << BigInt(width)) - 1n;
    if (value < minimum || value > maximum) throw new RangeError(`Value does not fit in ${width} bits.`);
    return value;
}

function fromBytes(bytes: Uint8Array, width: number, signed: boolean, order: ByteOrder): bigint {
    if (bytes.byteLength !== width / 8) throw new RangeError(`Expected exactly ${width / 8} bytes.`);
    let bits = 0n; for (let index = 0; index < bytes.length; index++) { const source = order === "big-endian" ? index : bytes.length - index - 1; bits = (bits << 8n) | BigInt(bytes[source]!); }
    if (signed && (bits & (1n << BigInt(width - 1))) !== 0n) return bits - modulus(width); return bits;
}

abstract class ConcreteWide<TSelf extends ConcreteWide<TSelf>> extends FixedWidthInteger<TSelf> {
    protected constructor(value: bigint | number | string) { super(BigInt(value)); }
}

export class UInt256 extends ConcreteWide<UInt256> { public readonly width = 256; public readonly signed = false; public constructor(value: bigint | number | string = 0n) { super(value); } protected create(value: bigint): UInt256 { return new UInt256(value); } public static parse(text: string, radix = 10): UInt256 { return new UInt256(parseFixed(text, radix, 256, false)); } public static fromBytes(bytes: Uint8Array, order: ByteOrder = "little-endian"): UInt256 { return new UInt256(fromBytes(bytes, 256, false, order)); } public static get minValue(): UInt256 { return new UInt256(0n); } public static get maxValue(): UInt256 { return new UInt256((1n << 256n) - 1n); } }
export class Int256 extends ConcreteWide<Int256> { public readonly width = 256; public readonly signed = true; public constructor(value: bigint | number | string = 0n) { super(value); } protected create(value: bigint): Int256 { return new Int256(value); } public static parse(text: string, radix = 10): Int256 { return new Int256(parseFixed(text, radix, 256, true)); } public static fromBytes(bytes: Uint8Array, order: ByteOrder = "little-endian"): Int256 { return new Int256(fromBytes(bytes, 256, true, order)); } public static get minValue(): Int256 { return new Int256(-(1n << 255n)); } public static get maxValue(): Int256 { return new Int256((1n << 255n) - 1n); } }
export class UInt512 extends ConcreteWide<UInt512> { public readonly width = 512; public readonly signed = false; public constructor(value: bigint | number | string = 0n) { super(value); } protected create(value: bigint): UInt512 { return new UInt512(value); } public static parse(text: string, radix = 10): UInt512 { return new UInt512(parseFixed(text, radix, 512, false)); } public static fromBytes(bytes: Uint8Array, order: ByteOrder = "little-endian"): UInt512 { return new UInt512(fromBytes(bytes, 512, false, order)); } public static get minValue(): UInt512 { return new UInt512(0n); } public static get maxValue(): UInt512 { return new UInt512((1n << 512n) - 1n); } }
export class Int512 extends ConcreteWide<Int512> { public readonly width = 512; public readonly signed = true; public constructor(value: bigint | number | string = 0n) { super(value); } protected create(value: bigint): Int512 { return new Int512(value); } public static parse(text: string, radix = 10): Int512 { return new Int512(parseFixed(text, radix, 512, true)); } public static fromBytes(bytes: Uint8Array, order: ByteOrder = "little-endian"): Int512 { return new Int512(fromBytes(bytes, 512, true, order)); } public static get minValue(): Int512 { return new Int512(-(1n << 511n)); } public static get maxValue(): Int512 { return new Int512((1n << 511n) - 1n); } }
export class UInt1024 extends ConcreteWide<UInt1024> { public readonly width = 1024; public readonly signed = false; public constructor(value: bigint | number | string = 0n) { super(value); } protected create(value: bigint): UInt1024 { return new UInt1024(value); } public static parse(text: string, radix = 10): UInt1024 { return new UInt1024(parseFixed(text, radix, 1024, false)); } public static fromBytes(bytes: Uint8Array, order: ByteOrder = "little-endian"): UInt1024 { return new UInt1024(fromBytes(bytes, 1024, false, order)); } public static get minValue(): UInt1024 { return new UInt1024(0n); } public static get maxValue(): UInt1024 { return new UInt1024((1n << 1024n) - 1n); } }
export class Int1024 extends ConcreteWide<Int1024> { public readonly width = 1024; public readonly signed = true; public constructor(value: bigint | number | string = 0n) { super(value); } protected create(value: bigint): Int1024 { return new Int1024(value); } public static parse(text: string, radix = 10): Int1024 { return new Int1024(parseFixed(text, radix, 1024, true)); } public static fromBytes(bytes: Uint8Array, order: ByteOrder = "little-endian"): Int1024 { return new Int1024(fromBytes(bytes, 1024, true, order)); } public static get minValue(): Int1024 { return new Int1024(-(1n << 1023n)); } public static get maxValue(): Int1024 { return new Int1024((1n << 1023n) - 1n); } }

export type WideInteger = UInt256 | Int256 | UInt512 | Int512 | UInt1024 | Int1024;

/** Fixed-width byte conversion helpers. */
export const BitConverterEx: {
    getBytes(value: WideInteger, order?: ByteOrder): Uint8Array;
    tryWriteBytes(value: WideInteger, destination: Uint8Array, order?: ByteOrder): boolean;
} = {
    getBytes(value: WideInteger, order: ByteOrder = "little-endian"): Uint8Array { return value.toBytes(order); },
    tryWriteBytes(value: WideInteger, destination: Uint8Array, order: ByteOrder = "little-endian"): boolean { const bytes = value.toBytes(order); if (destination.byteLength < bytes.byteLength) return false; destination.set(bytes); return true; },
};
