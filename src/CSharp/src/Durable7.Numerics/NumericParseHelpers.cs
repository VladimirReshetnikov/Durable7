// SPDX-License-Identifier: MIT

using System;
using System.Buffers;
using System.Globalization;
using System.Text;

namespace Durable7.Numerics;

/// <summary>
/// Shared parsing helpers for fixed-width numeric types in this assembly.
/// </summary>
/// <remarks>
/// The helpers are shared by all fixed-width integer implementations to keep style validation and
/// culture-sensitive sign-token handling behavior consistent across parse entry points.
/// </remarks>
internal static class NumericParseHelpers
{
    public delegate T Utf16ParseFunc<T>(ReadOnlySpan<char> text, NumberStyles style, IFormatProvider? provider);

    public delegate bool Utf16TryParseFunc<T>(
        ReadOnlySpan<char> text,
        NumberStyles style,
        IFormatProvider? provider,
        out T result);

    private const int StackCharBufferLimit = 256;

    private const NumberStyles SupportedHexStyles =
        NumberStyles.AllowLeadingWhite |
        NumberStyles.AllowTrailingWhite |
        NumberStyles.AllowHexSpecifier;

    private const NumberStyles InvalidStyles = ~(
        NumberStyles.AllowLeadingWhite | NumberStyles.AllowTrailingWhite |
        NumberStyles.AllowLeadingSign | NumberStyles.AllowTrailingSign |
        NumberStyles.AllowParentheses | NumberStyles.AllowDecimalPoint |
        NumberStyles.AllowThousands | NumberStyles.AllowExponent |
        NumberStyles.AllowCurrencySymbol | NumberStyles.AllowHexSpecifier |
        NumberStyles.AllowBinarySpecifier);

    /// <summary>
    /// Validates a <see cref="NumberStyles"/> combination using the same rules as the built-in integer parsers.
    /// </summary>
    /// <param name="style">The style flags to validate.</param>
    /// <exception cref="ArgumentException">
    /// <paramref name="style"/> contains undefined flags, or combines <see cref="NumberStyles.AllowHexSpecifier"/>
    /// or <see cref="NumberStyles.AllowBinarySpecifier"/> with flags outside the corresponding
    /// <see cref="NumberStyles.HexNumber"/> or <see cref="NumberStyles.BinaryNumber"/> combination.
    /// </exception>
    /// <remarks>
    /// Both <c>Parse</c> and <c>TryParse</c> call this validation and therefore throw for invalid style
    /// combinations, matching every BCL numeric type; <c>TryParse</c> returns <see langword="false"/> only for
    /// input-driven failures.
    /// </remarks>
    public static void ValidateStyle(NumberStyles style)
    {
        if ((style & InvalidStyles) != 0)
            throw new ArgumentException("An undefined NumberStyles value is being used.", nameof(style));

        if ((style & NumberStyles.AllowHexSpecifier) != 0 && (style & ~NumberStyles.HexNumber) != 0)
            throw new ArgumentException(
                "With the AllowHexSpecifier or AllowBinarySpecifier bit set in the enum bit field, the only other "
                + "valid bits that can be combined into the enum value must be a subset of those in HexNumber or "
                + "BinaryNumber.",
                nameof(style));

        if ((style & NumberStyles.AllowBinarySpecifier) != 0 && (style & ~NumberStyles.BinaryNumber) != 0)
            throw new ArgumentException(
                "With the AllowHexSpecifier or AllowBinarySpecifier bit set in the enum bit field, the only other "
                + "valid bits that can be combined into the enum value must be a subset of those in HexNumber or "
                + "BinaryNumber.",
                nameof(style));
    }

    /// <summary>
    /// Parses UTF-8 text through a temporary character span without allocating a string.
    /// </summary>
    /// <typeparam name="T">The result type.</typeparam>
    /// <param name="utf8Text">The UTF-8 input text.</param>
    /// <param name="style">The requested parse style.</param>
    /// <param name="provider">The format provider.</param>
    /// <param name="parse">The UTF-16 parser that owns the type-specific parse logic.</param>
    /// <returns>The parsed value.</returns>
    public static T ParseUtf8<T>(
        ReadOnlySpan<byte> utf8Text,
        NumberStyles style,
        IFormatProvider? provider,
        Utf16ParseFunc<T> parse)
    {
        int charCount = Encoding.UTF8.GetCharCount(utf8Text);
        if (charCount <= StackCharBufferLimit)
        {
            Span<char> buffer = stackalloc char[charCount];
            Encoding.UTF8.GetChars(utf8Text, buffer);
            return parse(buffer, style, provider);
        }

        char[] rented = ArrayPool<char>.Shared.Rent(charCount);
        try
        {
            Span<char> buffer = rented.AsSpan(0, charCount);
            Encoding.UTF8.GetChars(utf8Text, buffer);
            return parse(buffer, style, provider);
        }
        finally
        {
            ArrayPool<char>.Shared.Return(rented);
        }
    }

    /// <summary>
    /// Attempts to parse UTF-8 text through a temporary character span without allocating a string.
    /// </summary>
    /// <typeparam name="T">The result type.</typeparam>
    /// <param name="utf8Text">The UTF-8 input text.</param>
    /// <param name="style">The requested parse style.</param>
    /// <param name="provider">The format provider.</param>
    /// <param name="parse">The UTF-16 parser that owns the type-specific parse logic.</param>
    /// <param name="result">The parsed value when parsing succeeds; otherwise, the type default.</param>
    /// <returns><see langword="true"/> when parsing succeeds; otherwise, <see langword="false"/>.</returns>
    public static bool TryParseUtf8<T>(
        ReadOnlySpan<byte> utf8Text,
        NumberStyles style,
        IFormatProvider? provider,
        Utf16TryParseFunc<T> parse,
        out T result)
    {
        int charCount = Encoding.UTF8.GetCharCount(utf8Text);
        if (charCount <= StackCharBufferLimit)
        {
            Span<char> buffer = stackalloc char[charCount];
            Encoding.UTF8.GetChars(utf8Text, buffer);
            return parse(buffer, style, provider, out result);
        }

        char[] rented = ArrayPool<char>.Shared.Rent(charCount);
        try
        {
            Span<char> buffer = rented.AsSpan(0, charCount);
            Encoding.UTF8.GetChars(utf8Text, buffer);
            return parse(buffer, style, provider, out result);
        }
        finally
        {
            ArrayPool<char>.Shared.Return(rented);
        }
    }

    /// <summary>
    /// Applies the supported hexadecimal whitespace policy for fixed-width integer parsing.
    /// </summary>
    /// <param name="text">The input text.</param>
    /// <param name="style">The requested parse style.</param>
    /// <param name="normalized">The span after any allowed whitespace was trimmed.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="style"/> includes
    /// <see cref="NumberStyles.AllowHexSpecifier"/> and no unsupported flags. Leading and trailing whitespace are
    /// permitted only when the corresponding whitespace flags are set.
    /// </returns>
    public static bool TryNormalizeHexText(
        ReadOnlySpan<char> text,
        NumberStyles style,
        out ReadOnlySpan<char> normalized)
    {
        if ((style & NumberStyles.AllowHexSpecifier) == 0 || (style & ~SupportedHexStyles) != 0)
        {
            normalized = default;
            return false;
        }

        normalized = TrimByStyle(text, style);
        return true;
    }

    private static ReadOnlySpan<char> TrimByStyle(ReadOnlySpan<char> text, NumberStyles style)
    {
        if ((style & NumberStyles.AllowLeadingWhite) != 0)
            text = TrimStart(text);
        if ((style & NumberStyles.AllowTrailingWhite) != 0)
            text = TrimEnd(text);
        return text;
    }

    private static ReadOnlySpan<char> TrimStart(ReadOnlySpan<char> text)
    {
        var start = 0;
        while (start < text.Length && char.IsWhiteSpace(text[start]))
            start++;
        return text[start..];
    }

    private static ReadOnlySpan<char> TrimEnd(ReadOnlySpan<char> text)
    {
        var end = text.Length;
        while (end > 0 && char.IsWhiteSpace(text[end - 1]))
            end--;
        return text[..end];
    }

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
