/** Non-negative arbitrary integer with the SparseInteger arithmetic contract. */
export class SparseInteger {
    readonly #value: bigint;
    public constructor(value: bigint | number | string = 0n) { const parsed = BigInt(value); if (parsed < 0n) throw new RangeError("SparseInteger cannot be negative."); this.#value = parsed; }
    public static parse(text: string): SparseInteger { if (!/^\s*\+?\d+\s*$/.test(text)) throw new SyntaxError("Invalid SparseInteger."); return new SparseInteger(text.trim().replace(/^\+/, "")); }
    public static fromBigInt(value: bigint): SparseInteger { return new SparseInteger(value); }
    public static powerOfTwo(exponent: SparseInteger | bigint | number): SparseInteger { const value = exponent instanceof SparseInteger ? exponent.#value : BigInt(exponent); if (value < 0n || value > BigInt(Number.MAX_SAFE_INTEGER)) throw new RangeError("Exponent cannot be represented by this runtime."); return new SparseInteger(1n << value); }
    public get isZero(): boolean { return this.#value === 0n; }
    public toBigInt(): bigint { return this.#value; }
    public compareTo(other: SparseInteger): number { return this.#value < other.#value ? -1 : this.#value > other.#value ? 1 : 0; }
    public equals(other: SparseInteger): boolean { return this.#value === other.#value; }
    public add(other: SparseInteger): SparseInteger { return new SparseInteger(this.#value + other.#value); }
    public multiply(other: SparseInteger): SparseInteger { return new SparseInteger(this.#value * other.#value); }
    public exactLog2(): SparseInteger { if (this.#value === 0n || (this.#value & (this.#value - 1n)) !== 0n) throw new RangeError("The value is not an exact power of two."); return new SparseInteger(BigInt(this.#value.toString(2).length - 1)); }
    public toString(): string { return this.#value.toString(); }
}
