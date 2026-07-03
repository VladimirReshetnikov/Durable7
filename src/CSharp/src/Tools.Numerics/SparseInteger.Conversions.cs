// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Numerics;

namespace Tools.Numerics
{
    // Conversions to/from string and BigInteger, intended mostly for testing purposes.
    // See doc comments on the other part in SparseInteger.cs.
    public readonly partial struct SparseInteger
    {
        /// <summary>Returns a string representation of this value.</summary>
        /// <remarks>
        /// This method is intended mostly for testing purposes. Its implementation is delegated to
        /// <see cref="BigInteger.ToString(IFormatProvider)"/> invoked with <see cref="CultureInfo.InvariantCulture"/>.
        /// </remarks>
        /// <exception cref="OverflowException">
        /// This value is too large to be converted to <see cref="BigInteger"/>
        /// </exception>
        public override string ToString()
        {
            return ((BigInteger)this).ToString(CultureInfo.InvariantCulture);
        }

        /// <summary>
        /// Converts a string representation of a non-negative number to a value of type <see cref="SparseInteger"/>.
        /// </summary>
        /// <remarks>
        /// This method is intended mostly for testing purposes. Its implementation is delegated to
        /// <see cref="BigInteger.Parse(string, IFormatProvider)"/> invoked with
        /// <see cref="CultureInfo.InvariantCulture"/> as the second argument.
        /// </remarks>
        /// <param name="s">The string representation to convert.</param>
        /// <exception cref="ArgumentNullException"><paramref name="s"/> is <c>null</c>.</exception>
        /// <exception cref="FormatException"><paramref name="s"/> cannot be parsed as an integer.</exception>
        /// <exception cref="OverflowException"><paramref name="s"/> represents a negative number.</exception>
        public static SparseInteger Parse(string s)
        {
            return (SparseInteger) BigInteger.Parse(s, CultureInfo.InvariantCulture);
        }

        /// <summary>Converts <see cref="SparseInteger"/> to <see cref="string"/>.</summary>
        /// <remarks>
        /// This method is intended mostly for testing purposes. It is equivalent to <see cref="ToString"/> method.
        /// </remarks>
        /// <exception cref="OverflowException">
        /// This value is too large to be converted to <see cref="BigInteger"/>.
        /// </exception>
        /// <param name="x">The sparse integer to convert.</param>
        public static explicit operator string(SparseInteger x)
        {
            return x.ToString();
        }

        /// <summary>Converts <see cref="string"/> to <see cref="SparseInteger"/>.</summary>
        /// <remarks>
        /// This method is intended mostly for testing purposes. It is equivalent to <see cref="Parse"/> method.
        /// </remarks>
        /// <param name="s">A string to convert from.</param>
        /// <exception cref="ArgumentNullException"><paramref name="s"/> is <c>null</c>.</exception>
        /// <exception cref="FormatException"><paramref name="s"/> cannot be parsed as an integer.</exception>
        /// <exception cref="OverflowException"><paramref name="s"/> represents a negative number.</exception>
        public static explicit operator SparseInteger(string s)
        {
            return Parse(s);
        }

        /// <summary>Converts <see cref="SparseInteger"/> to <see cref="BigInteger"/>.</summary>
        /// <remarks>This method is intended mostly for testing purposes.</remarks>
        /// <exception cref="OverflowException">
        /// The position of the most significant bit exceeds <see cref="int.MaxValue"/>.
        /// </exception>
        /// <param name="x">The sparse integer to convert.</param>
        public static explicit operator BigInteger(SparseInteger x)
        {
            if (x.IsSmall)
            {
                return x.value;
            }

            BigInteger result = 0;
            foreach (var position in x.positions!)
            {
                if (!position.IsSmall || position.value > int.MaxValue)
                {
                    throw new OverflowException("The number is too large to be converted to BigInteger.");
                }

                result += BigInteger.Pow(2, (int) position.value);
            }

            return result;
        }

        /// <summary>Converts <see cref="BigInteger"/> to <see cref="SparseInteger"/>.</summary>
        /// <remarks>This method is intended mostly for testing purposes.</remarks>
        /// <exception cref="OverflowException"><paramref name="x"/> is a negative number.</exception>
        /// <param name="x">The arbitrary-precision integer to convert.</param>
        public static explicit operator SparseInteger(BigInteger x)
        {
            if (x < 0)
            {
                throw new OverflowException("A negative number cannot be converted to SparseInteger.");
            }

            if (x <= ulong.MaxValue)
            {
                return (ulong) x;
            }

            var positions = new List<SparseInteger>();
            ulong position = 0;
            while (x > 0)
            {
                if (!x.IsEven)
                {
                    positions.Add(position);
                }

                x >>= 1;
                position++;
            }

            return new SparseInteger(positions.ToArray());
        }
    }
}
