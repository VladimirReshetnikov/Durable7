// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;
using System.Numerics;
using System.Runtime.InteropServices;

namespace Tools.Numerics
{
    /// <summary>
    /// An value of this type represents a non-negative integer that can be very large (like those that occur as
    /// numeric values of high power towers, much larger than those that can be represented by <see cref="BigInteger"/>
    /// within feasible memory limits), subject to a condition that either:
    /// <list type="bullet">
    /// <item>
    /// the number fits into <see cref="ulong"/> type verbatim, or
    /// </item>
    /// <item>
    /// the number of 1's in its binary form is a moderate number (such that an array of that size can be allocated),
    /// and each of the positions of 1's in its binary form is, recursively, a number that can be represented
    /// by <see cref="SparseInteger"/>.
    /// </item>
    /// </list>
    /// </summary>
    /// <remarks>
    /// <para>This type is an immutable value type.</para>
    /// <para>
    /// An value of this type stores its numeric value as a sorted array of positions of 1's in its binary form,
    /// where each position is stored, recursively, as <see cref="SparseInteger"/>, except that the numeric value
    /// is stored verbatim in the field <see cref="value"/> of type <see cref="ulong"/> if it fits into that type
    /// (in the latter case property <see cref="IsSmall"/> returns <c>true</c>).
    /// </para>
    /// </remarks>
    [StructLayout(LayoutKind.Auto)]
    public readonly partial struct SparseInteger : IEquatable<SparseInteger>, IComparable<SparseInteger>
    {
        /// <summary>
        /// The numeric value of this value if it fits into <see cref="ulong"/> type, otherwise 0.
        /// </summary>
        private readonly ulong value;

        /// <summary>
        /// An sorted array of positions of 1's in the binary form of this number,
        /// stored from lowest (least significant) to highest (most significant).
        /// </summary>
        /// <remarks>
        /// If this field is <c>null</c>, then the field <see cref="value"/> of type <see cref="ulong"/> is used
        /// to represent such "small" number verbatim.
        /// </remarks>
        private readonly SparseInteger[]? positions;

        /// <summary>
        /// Initializes a value of <see cref="SparseInteger"/> type,
        /// from either:
        /// <list type="bullet">
        /// <item>
        /// a list of positions of 1's in its binary form (provided through <paramref name="positions"/>) or
        /// </item>
        /// <item>
        /// its verbatim representation in <see cref="ulong"/> type (provided through <paramref name="value"/>).
        /// </item>
        /// </list>
        /// </summary>
        /// <remarks>At most one of the parameters can have a non-default value.</remarks>
        private SparseInteger(SparseInteger[]? positions = default, ulong value = default)
        {
            Debug.Assert(value == 0 || positions == null);
            this.value = value;
            this.positions = positions;
        }

        /// <summary>Converts a 64-bit unsigned integer to a sparse integer.</summary>
        /// <param name="x">The value to convert.</param>
        public static implicit operator SparseInteger(ulong x) => new SparseInteger(value: x);

        /// <summary>
        /// Gets a boolean value indicating whether this value stores its numeric value verbatim
        /// in the field <see cref="value"/>. We refer to such values as "small".
        /// </summary>
        private bool IsSmall => this.positions == null;

        /// <summary>
        /// Returns the number of 1's in binary form of <paramref name="value"/> (i.e. the sum of its binary digits).
        /// </summary>
        /// <remarks>
        /// This is a classic implementation: https://en.wikipedia.org/wiki/Hamming_weight#Efficient_implementation.
        /// </remarks>
        private static byte GetHammingWeight(ulong value)
        {
            value = (value & 0x5555555555555555UL) + ((value >> 0x01) & 0x5555555555555555UL);
            value = (value & 0x3333333333333333UL) + ((value >> 0x02) & 0x3333333333333333UL);
            value = (value & 0x0F0F0F0F0F0F0F0FUL) + ((value >> 0x04) & 0x0F0F0F0F0F0F0F0FUL);
            value = (value & 0x00FF00FF00FF00FFUL) + ((value >> 0x08) & 0x00FF00FF00FF00FFUL);
            value = (value & 0x0000FFFF0000FFFFUL) + ((value >> 0x10) & 0x0000FFFF0000FFFFUL);
            value = (value & 0x00000000FFFFFFFFUL) + ((value >> 0x20) & 0x00000000FFFFFFFFUL);
            return (byte) value;
        }

        /// <summary>Gets a sorted array of positions of 1's in binary form of this number.</summary>
        /// <remarks>
        /// This property can be used both on "small" and "large" numbers (<see cref="IsSmall"/>).
        /// </remarks>
        private SparseInteger[] Positions
        {
            get
            {
                if (!this.IsSmall)
                {
                    return this.positions!;
                }

                var v = this.value;
                var positions = new SparseInteger[GetHammingWeight(v)];
                byte position = 0;
                byte index = 0;

                while (v > 0)
                {
                    if ((v & 1) == 1)
                    {
                        positions[index++] = position;
                    }

                    v >>= 1;
                    position++;
                }

                return positions;
            }
        }

        #region Hash, equality and comparison

        /// <summary>
        /// Returns a hash code for this value.
        /// </summary>
        public override int GetHashCode()
        {
            if (this.IsSmall)
            {
                return HashCode.Combine(this.value);
            }

            var hash = new HashCode();
            foreach (var position in this.positions!)
            {
                hash.Add(position);
            }

            return hash.ToHashCode();
        }

        /// <summary>
        /// Determines whether this value of type <see cref="SparseInteger"/> is equal to another value
        /// of the same type.
        /// </summary>
        /// <param name="other">Another value to compare this value with.</param>
        public bool Equals(SparseInteger other) => this.CompareTo(other) == 0;

        /// <summary>
        /// Determines whether this value of type <see cref="SparseInteger"/> is equal to a provided argument
        /// of type <see cref="ulong"/>.
        /// </summary>
        /// <param name="other">A <see cref="ulong"/> value to compare this value with.</param>
        public bool Equals(ulong other) => this.IsSmall && this.value == other;

        /// <summary>
        /// Determines whether this value of type <see cref="SparseInteger"/> is equal to another boxed value
        /// of the same type.
        /// </summary>
        /// <returns>
        /// <c>true</c> if parameter <paramref name="obj"/> is a boxed value of the type
        /// <see cref="SparseInteger"/> and this value is equal to that value; otherwise, <c>false</c>.
        /// </returns>
        /// <param name="obj">
        /// An object to compare this value with. Can be <c>null</c> (in which case the method returns <c>false</c>).
        /// </param>
        public override bool Equals(object? obj) => obj is SparseInteger other && this.Equals(other);

        /// <summary>
        /// Compares this value of type <see cref="SparseInteger"/> with another value of the same type.
        /// </summary>
        /// <returns>
        /// An <see cref="int"/> value indicating the relative order of the values being compared:
        /// <list type="bullet">
        /// <item>
        /// A negative value means that the current value is numerically less than the <paramref name="other"/>.
        /// </item>
        /// <item>
        /// The value 0 means that the current value is numerically equal to the <paramref name="other"/>.
        /// </item>
        /// <item>
        /// A positive value means that the current value is numerically greater than the <paramref name="other"/>.
        /// </item>
        /// </list>
        /// </returns>
        /// <param name="other">A value of type <see cref="SparseInteger"/> to compare with this value.</param>
        /// <remarks>
        /// </remarks>
        public int CompareTo(SparseInteger other)
        {
            if (this.IsSmall)
            {
                if (other.IsSmall)
                {
                    return this.value.CompareTo(other.value);
                }

                // values that fit into ulong type are always stored in the value field
                return -1;
            }

            if (other.IsSmall)
            {
                // values that fit into ulong type are always stored in the value field
                return 1;
            }

            // fast reference equality check
            if (this.positions == other.positions)
            {
                return 0;
            }

            // Compare bitwise starting from the highest (most significant) bit
            SparseInteger[] thisPositions = this.positions!;
            SparseInteger[] otherPositions = other.positions!;

            for (int thisPosition = thisPositions.Length - 1, otherPosition = otherPositions.Length - 1;;)
            {
                var result = thisPositions[thisPosition].CompareTo(otherPositions[otherPosition]);
                if (result != 0)
                {
                    return result;
                }

                if (--thisPosition < 0)
                {
                    return
                        --otherPosition < 0
                            ? 0
                            : -1; // all compared bits are identical, but the other number has more bits
                }

                if (--otherPosition < 0)
                {
                    // all compared bits are identical, but this number has more bits
                    return 1;
                }
            }
        }

        /// <summary>Determines whether a sparse integer and a 64-bit unsigned integer have the same value.</summary>
        /// <param name="x">The sparse integer to compare.</param>
        /// <param name="y">The 64-bit unsigned integer to compare.</param>
        public static bool operator ==(SparseInteger x, ulong y) => x.Equals(y);

        /// <summary>Determines whether a sparse integer and a 64-bit unsigned integer have different values.</summary>
        /// <param name="x">The sparse integer to compare.</param>
        /// <param name="y">The 64-bit unsigned integer to compare.</param>
        public static bool operator !=(SparseInteger x, ulong y) => !x.Equals(y);

        /// <summary>Determines whether two sparse integers have the same value.</summary>
        /// <param name="x">The first sparse integer to compare.</param>
        /// <param name="y">The second sparse integer to compare.</param>
        public static bool operator ==(SparseInteger x, SparseInteger y) => x.CompareTo(y) == 0;

        /// <summary>Determines whether two sparse integers have different values.</summary>
        /// <param name="x">The first sparse integer to compare.</param>
        /// <param name="y">The second sparse integer to compare.</param>
        public static bool operator !=(SparseInteger x, SparseInteger y) => x.CompareTo(y) != 0;

        /// <summary>Determines whether one sparse integer is less than another sparse integer.</summary>
        /// <param name="x">The first sparse integer to compare.</param>
        /// <param name="y">The second sparse integer to compare.</param>
        public static bool operator <(SparseInteger x, SparseInteger y) => x.CompareTo(y) < 0;

        /// <summary>Determines whether one sparse integer is greater than another sparse integer.</summary>
        /// <param name="x">The first sparse integer to compare.</param>
        /// <param name="y">The second sparse integer to compare.</param>
        public static bool operator >(SparseInteger x, SparseInteger y) => x.CompareTo(y) > 0;

        /// <summary>Determines whether one sparse integer is less than or equal to another sparse integer.</summary>
        /// <param name="x">The first sparse integer to compare.</param>
        /// <param name="y">The second sparse integer to compare.</param>
        public static bool operator <=(SparseInteger x, SparseInteger y) => x.CompareTo(y) <= 0;

        /// <summary>Determines whether one sparse integer is greater than or equal to another sparse integer.</summary>
        /// <param name="x">The first sparse integer to compare.</param>
        /// <param name="y">The second sparse integer to compare.</param>
        public static bool operator >=(SparseInteger x, SparseInteger y) => x.CompareTo(y) >= 0;

        #endregion

        #region Arithmetic

        /// <summary>
        /// Returns this number plus 1.
        /// </summary>
        private SparseInteger PlusOne()
        {
            if (this.IsSmall)
            {
                return this.value < ulong.MaxValue
                    ? this.value + 1
                    : new SparseInteger(new SparseInteger[] {64});
            }

            var positionsNew = ArrayHelpers.RemoveElement(this.positions, (SparseInteger) 0, out bool removed);

            return removed
                // the lowest bit was set: clear it and carry into bit 1 (adding 2 propagates further carries)
                ? new SparseInteger(positionsNew) + 2

                // the lowest bit was not set: set it
                : new SparseInteger(ArrayHelpers.InsertElement(positionsNew, (SparseInteger) 0));
        }

        /// <summary>
        /// Adds numbers <paramref name="x"/> and <paramref name="y"/> and returns the result.
        /// </summary>
        public static SparseInteger operator +(SparseInteger x, SparseInteger y)
        {
            if (x == 0)
            {
                return y;
            }

            if (y == 0)
            {
                return x;
            }

            if (x.IsSmall && y.IsSmall)
            {
                var sum = x.value + y.value;
                if (sum > x.value) // if no overflow
                {
                    return sum;
                }
            }

            SparseInteger[] xPositions = x.Positions;
            SparseInteger[] yPositions = y.Positions;
            var merged = new SparseInteger[xPositions.Length + yPositions.Length + 1];
            int xIndex = 0;
            int yIndex = 0;
            int mergedCount = 0;
            SparseInteger? carry = null;

            while (xIndex < xPositions.Length || yIndex < yPositions.Length || carry.HasValue)
            {
                SparseInteger position;
                if (xIndex < xPositions.Length)
                {
                    position = xPositions[xIndex];
                }
                else if (yIndex < yPositions.Length)
                {
                    position = yPositions[yIndex];
                }
                else
                {
                    position = carry!.Value;
                }

                if (yIndex < yPositions.Length && yPositions[yIndex] < position)
                {
                    position = yPositions[yIndex];
                }

                if (carry.HasValue && carry.Value < position)
                {
                    position = carry.Value;
                }

                int occurrences = 0;
                if (xIndex < xPositions.Length && xPositions[xIndex] == position)
                {
                    xIndex++;
                    occurrences++;
                }

                if (yIndex < yPositions.Length && yPositions[yIndex] == position)
                {
                    yIndex++;
                    occurrences++;
                }

                if (carry.HasValue && carry.Value == position)
                {
                    carry = null;
                    occurrences++;
                }

                if ((occurrences & 1) != 0)
                {
                    merged[mergedCount++] = position;
                }

                if (occurrences >= 2)
                {
                    carry = position.PlusOne();
                }
            }

            if (mergedCount != merged.Length)
            {
                Array.Resize(ref merged, mergedCount);
            }

            return new SparseInteger(merged);
        }

        /// <summary>
        /// Multiplies numbers <paramref name="x"/> and <paramref name="y"/> and returns the result.
        /// </summary>
        public static SparseInteger operator *(SparseInteger x, SparseInteger y)
        {
            if (x == 0 || y == 0)
            {
                return 0;
            }

            if (x == 1)
            {
                return y;
            }

            if (y == 1)
            {
                return x;
            }

            if (x.IsSmall && y.IsSmall)
            {
                var product = x.value * y.value;
                if (product / y.value == x.value) // if no overflow
                {
                    return product;
                }
            }

            SparseInteger result = 0;
            foreach (var position in y.Positions)
            {
                result += x.MultiplyByExp2(position);
            }

            return result;
        }

        #endregion

        #region Powers and logarithms

        /// <summary>
        /// Returns 2 raised to the power of this number.
        /// </summary>
        public SparseInteger Exp2()
        {
            return this.IsSmall && this.value < 64
                ? 1UL << (byte) this.value
                : new SparseInteger(new[] {this});
        }

        /// <summary>
        /// Returns a base-2 logarithm of this number if is an exact power of 2.
        /// </summary>
        /// <exception cref="InvalidOperationException">This number is not an exact power of 2.</exception>
        public SparseInteger Log2()
        {
            if (!this.IsSmall)
            {
                SparseInteger[] positions = this.positions!;

                if (positions.Length != 1)
                {
                    throw new InvalidOperationException("This number is not an exact power of two.");
                }

                return positions[0];
            }

            var v = this.value;
            if (GetHammingWeight(v) != 1)
            {
                throw new InvalidOperationException("This number is not an exact power of two.");
            }

            byte log2 = 0;
            while ((v & 1) == 0)
            {
                v >>= 1;
                log2++;
            }

            return log2;
        }

        #endregion


        /// <summary>
        /// The result of <c>this.MultiplyByExp2(x)</c> is equivalent to <c>this * x.Exp2()</c>,
        /// but can be faster in many cases.
        /// </summary>
        private SparseInteger MultiplyByExp2(SparseInteger power)
        {
            if (this == 0 || power == 0)
            {
                return this;
            }

            if (this.IsSmall && power.IsSmall && power.value < 63)
            {
                var result = this.value << (byte) power.value;
                if (result >> (byte) power.value == this.value) // if no overflow
                {
                    return result;
                }
            }

            var positions = this.Positions;
            var positionsNew = new SparseInteger[positions.Length];
            for (int i = 0; i < positions.Length; i++)
            {
                positionsNew[i] = positions[i] + power;
            }

            return new SparseInteger(positionsNew);
        }

        /// <summary>
        /// Raises this value to the power <paramref name="exponent"/>.
        /// Requires this value to be an exact power of 2.
        /// </summary>
        /// <exception cref="InvalidOperationException">This value is not an exact power of 2.</exception>
        public SparseInteger Power(SparseInteger exponent)
        {
            return (this.Log2() * exponent).Exp2();
        }
    }
}
