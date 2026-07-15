import { describe, expect, test } from "vitest";
import fc from "fast-check";
import {
    BitConverterEx,
    Int256,
    Int512,
    Int1024,
    SparseInteger,
    UInt256,
    UInt512,
    UInt1024,
    type FixedWidthInteger,
} from "../../src/numerics/index.js";

interface WideConstructor<T extends FixedWidthInteger<T>> {
    new(value?: bigint | number | string): T;
    readonly minValue: T;
    readonly maxValue: T;
    parse(text: string, radix?: number): T;
    fromBytes(bytes: Uint8Array, order?: "little-endian" | "big-endian"): T;
}

function describeWidth<U extends FixedWidthInteger<U>, S extends FixedWidthInteger<S>>(
    width: number,
    Unsigned: WideConstructor<U>,
    Signed: WideConstructor<S>,
): void {
describe(`${width}-bit integers`, () => {
    test("wrap unchecked arithmetic and reject checked overflow", () => {
        const max = Unsigned.maxValue;
        expect(max.add(new Unsigned(1n)).toBigInt()).toBe(0n);
        expect(() => max.checkedAdd(new Unsigned(1n))).toThrow(RangeError);
        const signedMax = Signed.maxValue;
        expect(signedMax.add(new Signed(1n)).toBigInt()).toBe(Signed.minValue.toBigInt());
        expect(() => signedMax.checkedAdd(new Signed(1n))).toThrow(RangeError);
    });

    test("preserve two's-complement parse and byte round trips", () => {
        const minusOne = new Signed(-1n);
        expect(Signed.parse("f".repeat(width / 4), 16).toBigInt()).toBe(-1n);
        expect(Signed.fromBytes(minusOne.toBytes("little-endian"), "little-endian").toBigInt()).toBe(-1n);
        expect(Signed.fromBytes(minusOne.toBytes("big-endian"), "big-endian").toBigInt()).toBe(-1n);
        expect(minusOne.toBytes().every((byte) => byte === 0xff)).toBe(true);
    });

    test("differential arithmetic matches normalized BigInt", () => {
        fc.assert(fc.property(fc.bigInt({ min: -(1n << 200n), max: 1n << 200n }), fc.bigInt({ min: -(1n << 200n), max: 1n << 200n }), (left, right) => {
            const mod = 1n << BigInt(width);
            const normalize = (value: bigint): bigint => ((value % mod) + mod) % mod;
            const a = new Unsigned(left); const b = new Unsigned(right);
            expect(a.add(b).toUnsignedBigInt()).toBe(normalize(left + right));
            expect(a.subtract(b).toUnsignedBigInt()).toBe(normalize(left - right));
            expect(a.multiply(b).toUnsignedBigInt()).toBe(normalize(left * right));
        }), { numRuns: 100 });
    });
});
}

describeWidth(256, UInt256, Int256);
describeWidth(512, UInt512, Int512);
describeWidth(1024, UInt1024, Int1024);

describe("wide integer common APIs", () => {
    test("formatting, shifts, rotations, and fixed byte conversion", () => {
        const value = new UInt256(0x1234n);
        expect(value.format("X8")).toBe("00001234");
        expect(new Int256(-12345).format("N0")).toBe("-12,345");
        expect(value.rotateLeft(4).rotateRight(4).equals(value)).toBe(true);
        expect(value.getByteCount()).toBe(32);
        const destination = new Uint8Array(32);
        expect(BitConverterEx.tryWriteBytes(value, destination)).toBe(true);
        expect(UInt256.fromBytes(destination).equals(value)).toBe(true);
        expect(BitConverterEx.tryWriteBytes(value, new Uint8Array(31))).toBe(false);
    });

    test("division follows signed overflow and truncation contracts", () => {
        expect(new Int256(-7).divide(new Int256(3)).toBigInt()).toBe(-2n);
        expect(new Int256(-7).remainder(new Int256(3)).toBigInt()).toBe(-1n);
        expect(() => Int256.minValue.divide(new Int256(-1))).toThrow(RangeError);
        expect(() => new UInt256(1).divide(new UInt256(0))).toThrow(RangeError);
    });

    test("bit diagnostics, predicates, div-rem, and increments follow fixed-width bits", () => {
        const value = new UInt256(0b1011_0000n);
        expect([value.leadingZeroCount(), value.trailingZeroCount(), value.popCount(), value.log2()]).toEqual([248, 4, 3, 7]);
        expect(value.isEven).toBe(true);
        expect(new UInt256(1n << 200n).isPowerOfTwo).toBe(true);
        expect(UInt256.maxValue.increment().isZero).toBe(true);
        const division = new Int256(-7).divRem(new Int256(3));
        expect([division.quotient.toBigInt(), division.remainder.toBigInt()]).toEqual([-2n, -1n]);
        expect(new Int256(-7).abs().toBigInt()).toBe(7n);
        expect(() => Int256.minValue.abs()).toThrow(RangeError);
    });
});

describe("SparseInteger", () => {
    test("supports sparse powers and arithmetic", () => {
        const power = SparseInteger.powerOfTwo(1000);
        expect(power.exactLog2().toBigInt()).toBe(1000n);
        expect(power.add(new SparseInteger(1)).toBigInt()).toBe((1n << 1000n) + 1n);
        expect(new SparseInteger(123).multiply(new SparseInteger(456)).toString()).toBe(String(123 * 456));
        expect(SparseInteger.parse("  +123456 ").toBigInt()).toBe(123456n);
        expect(() => new SparseInteger(-1)).toThrow(RangeError);
    });
});

// Compile-time assertion that all width classes retain the common contract.
const _commonSurface: FixedWidthInteger<UInt256> = new UInt256();
void _commonSurface;
