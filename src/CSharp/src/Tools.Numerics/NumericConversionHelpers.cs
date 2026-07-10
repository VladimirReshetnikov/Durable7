// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Tools.Numerics;

/// <summary>Shared floating-point conversion policy for the fixed-width integer family.</summary>
internal static class NumericConversionHelpers
{
    /// <summary>Truncates and clamps or checks a binary floating-point value against integer bounds.</summary>
    /// <param name="value">The floating-point value to convert.</param>
    /// <param name="minValue">The inclusive destination lower bound.</param>
    /// <param name="maxValue">The inclusive destination upper bound.</param>
    /// <param name="isChecked">Whether non-finite and out-of-range inputs throw instead of clamping.</param>
    /// <returns>The truncated in-range value, or the selected bound for an unchecked out-of-range input.</returns>
    /// <exception cref="OverflowException"><paramref name="isChecked"/> is true and the input is non-finite or out of range.</exception>
    public static BigInteger FromFloatingPoint(
        double value,
        BigInteger minValue,
        BigInteger maxValue,
        bool isChecked)
    {
        if (double.IsNaN(value))
        {
            if (isChecked)
                throw new OverflowException();
            return BigInteger.Zero;
        }

        if (double.IsNegativeInfinity(value))
        {
            if (isChecked)
                throw new OverflowException();
            return minValue;
        }

        if (double.IsPositiveInfinity(value))
        {
            if (isChecked)
                throw new OverflowException();
            return maxValue;
        }

        BigInteger truncated = new(value);
        if (truncated < minValue)
        {
            if (isChecked)
                throw new OverflowException();
            return minValue;
        }

        if (truncated > maxValue)
        {
            if (isChecked)
                throw new OverflowException();
            return maxValue;
        }

        return truncated;
    }
}
