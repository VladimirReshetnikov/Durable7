// SPDX-License-Identifier: MIT

using System;
using System.Globalization;

namespace Tools.Numerics;

/// <summary>
/// Shared parsing helpers for fixed-width numeric types in this assembly.
/// </summary>
/// <remarks>
/// The helpers are shared by both 256-bit and 512-bit integer implementations to keep culture-sensitive sign-token
/// handling behavior consistent across parse entry points.
/// </remarks>
internal static class NumericParseHelpers
{
    /// <summary>
    /// Removes a culture-specific leading sign token when present.
    /// </summary>
    /// <param name="text">The text that may begin with a sign token.</param>
    /// <param name="numberFormat">The culture-specific sign token source.</param>
    /// <param name="unsignedText">The remaining text after an optional sign token is removed.</param>
    /// <param name="isNegative">Indicates whether the removed sign token represented a negative sign.</param>
    /// <returns><see langword="true"/> when a sign token was removed; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// <para>
    /// The method checks the culture-specific positive sign before the negative sign. This preserves deterministic
    /// behavior for cultures whose sign tokens share prefixes (for example, custom format providers where both signs
    /// start with the same character sequence).
    /// </para>
    /// <para>
    /// The method does not trim whitespace and does not validate the remaining digits; callers are expected to invoke
    /// it only after applying any style-specific whitespace policy and before digit-level parsing.
    /// </para>
    /// </remarks>
    /// <example>
    /// <code>
    /// NumberFormatInfo nfi = (NumberFormatInfo)CultureInfo.InvariantCulture.NumberFormat.Clone();
    /// nfi.PositiveSign = "+";
    /// nfi.NegativeSign = "-";
    ///
    /// bool hadSign = NumericParseHelpers.TryStripLeadingSign("-42", nfi, out ReadOnlySpan&lt;char&gt; unsigned, out bool isNegative);
    /// // hadSign == true
    /// // isNegative == true
    /// // unsigned.SequenceEqual("42") == true
    /// </code>
    /// </example>
    public static bool TryStripLeadingSign(
        ReadOnlySpan<char> text,
        NumberFormatInfo numberFormat,
        out ReadOnlySpan<char> unsignedText,
        out bool isNegative)
    {
        ReadOnlySpan<char> positiveSign = numberFormat.PositiveSign.AsSpan();
        if (!positiveSign.IsEmpty && text.StartsWith(positiveSign, StringComparison.Ordinal))
        {
            unsignedText = text[positiveSign.Length..];
            isNegative = false;
            return true;
        }

        ReadOnlySpan<char> negativeSign = numberFormat.NegativeSign.AsSpan();
        if (!negativeSign.IsEmpty && text.StartsWith(negativeSign, StringComparison.Ordinal))
        {
            unsignedText = text[negativeSign.Length..];
            isNegative = true;
            return true;
        }

        unsignedText = text;
        isNegative = false;
        return false;
    }
}
