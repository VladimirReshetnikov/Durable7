// SPDX-License-Identifier: MIT

using System.Globalization;

namespace Tools.Numerics;

/// <summary>Shared helpers for standard fixed-width integer formatting.</summary>
internal static class NumericFormatHelpers
{
    /// <summary>Applies the precision component of a hexadecimal standard format string.</summary>
    /// <param name="digits">The unpadded hexadecimal digits.</param>
    /// <param name="format">The complete standard format string.</param>
    /// <returns><paramref name="digits"/> padded with leading zeroes to the requested minimum width.</returns>
    /// <exception cref="FormatException">The precision component is not a non-negative decimal integer.</exception>
    public static string ApplyHexPrecision(string digits, string format)
    {
        if (format.Length == 1)
            return digits;

        if (!int.TryParse(format.AsSpan(1), NumberStyles.None, CultureInfo.InvariantCulture, out int precision))
            throw new FormatException();

        return precision > digits.Length ? digits.PadLeft(precision, '0') : digits;
    }
}
