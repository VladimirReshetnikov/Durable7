// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using static Tools.Numerics.BitHelpers;

namespace Tools.Numerics;

/// <summary>
/// Represents an unsigned 256-bit integer with deterministic arithmetic, parsing, formatting, and endian semantics.
/// </summary>
/// <remarks>
/// <para>
/// The value is represented by two <see cref="UInt128"/> halves. Arithmetic and bitwise operations are implemented
/// directly over those halves, including multiplication/division/modulus and decimal/hex text conversion paths.
/// </para>
/// <para>
/// The binary layout is little-endian by halves: <c>_lower</c> stores bits 0..127 and <c>_upper</c> stores bits
/// 128..255.
/// </para>
/// <para>
/// This type is intentionally designed to mirror the ergonomics of built-in integer primitives: operators are value-
/// based, parsing and formatting follow .NET numeric conventions, and endian helpers encode a canonical 32-byte wire
/// representation suitable for deterministic persistence and protocol payloads.
/// </para>
/// <para>
    /// The implementation is intended to be aligned with <see cref="System.UInt128"/> behavior. It should also be reasonably symmetric with its signed
/// counterpart <see cref="Int256"/> (in this project).
/// </para>
/// </remarks>
[StructLayout(LayoutKind.Sequential)]
[DebuggerDisplay("{ToString(),nq}")]
public readonly struct UInt256 :
    IComparable,
    IComparable<UInt256>,
    IEquatable<UInt256>,
    ISpanFormattable,
    IUtf8SpanFormattable,
    IParsable<UInt256>,
    ISpanParsable<UInt256>,
    IMinMaxValue<UInt256>
{
    /// <summary>Total bit width of the unsigned representation.</summary>
    private const int Bits = 256;

    /// <summary>Total byte width of the unsigned representation.</summary>
    private const int Bytes = Bits / 8;

    /// <summary>
    /// Cached inclusive upper bound used by checked <see cref="BigInteger"/> conversions.
    /// </summary>
    private static readonly BigInteger s_bigMaxValue = (BigInteger.One << Bits) - BigInteger.One;

    /// <summary>The most significant 128 bits (bit positions 128 through 255).</summary>
    private readonly UInt128 _upper; // bits 128..255

    /// <summary>The least significant 128 bits (bit positions 0 through 127).</summary>
    private readonly UInt128 _lower; // bits 0..127

    /// <summary>Initializes a new <see cref="UInt256"/> from its upper and lower 128-bit halves.</summary>
    /// <param name="upper">The most significant 128 bits (bits 128 through 255).</param>
    /// <param name="lower">The least significant 128 bits (bits 0 through 127).</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public UInt256(UInt128 upper, UInt128 lower) => (_upper, _lower) = (upper, lower);

    /// <summary>Initializes a new <see cref="UInt256"/> from a 64-bit value.</summary>
    /// <param name="value">The initial numeric value.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public UInt256(ulong value) => (_upper, _lower) = (0, value);

    /// <summary>Gets the upper 128-bit half (bits 128 through 255).</summary>
    internal UInt128 Upper => _upper;

    /// <summary>Gets the lower 128-bit half (bits 0 through 127).</summary>
    internal UInt128 Lower => _lower;

    /// <summary>Represents the additive identity (0).</summary>
    public static readonly UInt256 Zero = default;

    /// <summary>Represents the multiplicative identity (1).</summary>
    public static readonly UInt256 One = new(0, 1);

    /// <summary>Represents the smallest possible <see cref="UInt256"/> value.</summary>
    public static readonly UInt256 MinValue = Zero;

    /// <summary>Represents the largest possible <see cref="UInt256"/> value.</summary>
    public static readonly UInt256 MaxValue = new(UInt128.MaxValue, UInt128.MaxValue);

    static UInt256 IMinMaxValue<UInt256>.MinValue => MinValue;

    static UInt256 IMinMaxValue<UInt256>.MaxValue => MaxValue;

    static UInt256 IParsable<UInt256>.Parse(string s, IFormatProvider? provider) => Parse(s, provider);

    static bool IParsable<UInt256>.TryParse(string? s, IFormatProvider? provider, out UInt256 result) =>
        TryParse(s, NumberStyles.Integer, provider, out result);

    static UInt256 ISpanParsable<UInt256>.Parse(ReadOnlySpan<char> s, IFormatProvider? provider) =>
        Parse(s, NumberStyles.Integer, provider);

    static bool ISpanParsable<UInt256>.TryParse(ReadOnlySpan<char> s, IFormatProvider? provider, out UInt256 result) =>
        TryParse(s, NumberStyles.Integer, provider, out result);

    /// <summary>Gets a value indicating whether the current value is equal to zero.</summary>
    public bool IsZero => _upper == 0 && _lower == 0;

    /// <summary>Compares the current value with another value of the same type.</summary>
    /// <param name="other">The value to compare with this instance.</param>
    /// <returns>
    /// A signed integer that indicates the relative order of this instance and <paramref name="other"/>.
    /// </returns>
    public int CompareTo(UInt256 other)
    {
        int c = _upper.CompareTo(other._upper);
        return c != 0 ? c : _lower.CompareTo(other._lower);
    }

    /// <summary>
    /// Compares this instance to a boxed value.
    /// </summary>
    /// <param name="obj">The boxed value to compare with this instance.</param>
    /// <returns>
    /// A signed integer that indicates the relative order of this instance and <paramref name="obj"/>.
    /// </returns>
    /// <exception cref="ArgumentException"><paramref name="obj"/> is not a <see cref="UInt256"/>.</exception>
    int IComparable.CompareTo(object? obj) =>
        obj switch
        {
            null => 1,
            UInt256 other => CompareTo(other),
            _ => throw new ArgumentException("Object must be a UInt256.")
        };

    /// <summary>Determines whether the current value is equal to another <see cref="UInt256"/> value.</summary>
    /// <param name="other">The value to compare with this instance.</param>
    /// <returns>
    /// <see langword="true"/> when both 128-bit halves are equal; otherwise, <see langword="false"/>.
    /// </returns>
    public bool Equals(UInt256 other) => (_upper, _lower) == (other._upper, other._lower);

    /// <summary>Determines whether the current value is equal to a specified object.</summary>
    /// <param name="obj">The object to compare with this instance.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="obj"/> is a <see cref="UInt256"/> with the same value.
    /// </returns>
    public override bool Equals([NotNullWhen(true)] object? obj) => obj is UInt256 other && Equals(other);

    /// <summary>Returns a hash code for the current value.</summary>
    /// <returns>A hash code derived from the upper and lower halves.</returns>
    public override int GetHashCode() => HashCode.Combine(_upper, _lower);

    /// <summary>Converts the numeric value to its equivalent string representation.</summary>
    public override string ToString() => FormatDecimal(this, provider: null);

    /// <summary>
    /// Converts the numeric value to its equivalent string representation
    /// using the specified format and culture-specific format information.
    /// </summary>
    /// <param name="format">
    /// A standard numeric format specifier with optional precision (for example <c>G</c>, <c>D5</c>, <c>N0</c>,
    /// or <c>X8</c>) that selects the textual representation.
    /// </param>
    /// <param name="formatProvider">
    /// An object that supplies culture-specific formatting information used for decimal/general formatting tokens.
    /// </param>
    public string ToString(string? format, IFormatProvider? formatProvider) => FormatValue(this, format, formatProvider);

    /// <summary>Formats the value into a character span.</summary>
    /// <param name="destination">
    /// The character buffer that receives the formatted decimal or hexadecimal representation.
    /// </param>
    /// <param name="charsWritten">
    /// When this method returns, contains the number of characters written to <paramref name="destination"/>.
    /// </param>
    /// <param name="format">
    /// A standard numeric format specifier with optional precision (for example <c>G</c>, <c>D5</c>, <c>N0</c>,
    /// or <c>X8</c>) that selects the textual representation.
    /// </param>
    /// <param name="provider">
    /// An object that supplies culture-specific formatting information used for decimal/general formatting tokens.
    /// </param>
    public bool TryFormat(
        Span<char> destination,
        out int charsWritten,
        ReadOnlySpan<char> format = default,
        IFormatProvider? provider = null)
    {
        string s = FormatValue(this, format.IsEmpty ? null : new string(format), provider);
        if (s.Length > destination.Length)
        {
            charsWritten = 0;
            return false;
        }

        s.AsSpan().CopyTo(destination);
        charsWritten = s.Length;
        return true;
    }

    /// <summary>
    /// Formats into UTF-8 bytes (mirrors the <see cref="IUtf8SpanFormattable"/> shape used by built-in integers).
    /// </summary>
    /// <param name="utf8Destination">
    /// The UTF-8 byte buffer that receives the formatted decimal or hexadecimal representation.
    /// </param>
    /// <param name="bytesWritten">
    /// When this method returns, contains the number of bytes written to <paramref name="utf8Destination"/>.
    /// </param>
    /// <param name="format">
    /// A standard numeric format specifier with optional precision (for example <c>G</c>, <c>D5</c>, <c>N0</c>,
    /// or <c>X8</c>) that selects the textual representation.
    /// </param>
    /// <param name="provider">
    /// An object that supplies culture-specific formatting information used for decimal/general formatting tokens.
    /// </param>
    public bool TryFormat(
        Span<byte> utf8Destination,
        out int bytesWritten,
        ReadOnlySpan<char> format = default,
        IFormatProvider? provider = null)
    {
        return Encoding.UTF8.TryGetBytes(
            FormatValue(this, format.IsEmpty ? null : new string(format), provider),
            utf8Destination,
            out bytesWritten);
    }

    #region Parsing

    /// <summary>Converts the string representation of a number to its <see cref="UInt256"/> equivalent.</summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <returns>The parsed unsigned 256-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="s"/> is <see langword="null"/>.</exception>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> does not represent a valid integer in the default style.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[0, 2^256 - 1]</c>.
    /// </exception>
    public static UInt256 Parse(string s) => Parse(s, NumberStyles.Integer, CultureInfo.CurrentCulture);

    /// <summary>
    /// Converts the string representation of a number to its <see cref="UInt256"/> equivalent using the specified
    /// format provider.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed unsigned 256-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="s"/> is <see langword="null"/>.</exception>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> does not represent a valid integer in the default style.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[0, 2^256 - 1]</c>.
    /// </exception>
    public static UInt256 Parse(string s, IFormatProvider? provider) => Parse(s, NumberStyles.Integer, provider);

    /// <summary>
    /// Converts the string representation of a number in a specified style and culture-specific format to its <see cref="UInt256"/> equivalent.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed unsigned 256-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="s"/> is <see langword="null"/>.</exception>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> is empty or does not represent a valid value under <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[0, 2^256 - 1]</c>.
    /// </exception>
    public static UInt256 Parse(string s, NumberStyles style, IFormatProvider? provider = null)
    {
        ArgumentNullException.ThrowIfNull(s);
        return ParseCore(s.AsSpan(), style, provider);
    }

    /// <summary>
    /// Converts the span representation of a number in a specified style and culture-specific format to its <see cref="UInt256"/> equivalent.
    /// </summary>
    /// <param name="s">A character span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed unsigned 256-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> is empty or does not represent a valid value under <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[0, 2^256 - 1]</c>.
    /// </exception>
    public static UInt256 Parse(
        ReadOnlySpan<char> s,
        NumberStyles style = NumberStyles.Integer,
        IFormatProvider? provider = null)
    {
        return ParseCore(s, style, provider);
    }

    /// <summary>
    /// Converts the UTF-8 span representation of a number in a specified style and culture-specific format to its <see cref="UInt256"/> equivalent.
    /// </summary>
    /// <param name="utf8Text">A UTF-8 byte span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed unsigned 256-bit value represented by <paramref name="utf8Text"/>.</returns>
    /// <exception cref="FormatException">
    /// <paramref name="utf8Text"/> is empty or does not represent a valid value under <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="utf8Text"/> represents a value outside the inclusive range <c>[0, 2^256 - 1]</c>.
    /// </exception>
    public static UInt256 Parse(
        ReadOnlySpan<byte> utf8Text,
        NumberStyles style = NumberStyles.Integer,
        IFormatProvider? provider = null)
    {
        return NumericParseHelpers.ParseUtf8(utf8Text, style, provider, ParseCore);
    }

    /// <summary>
    /// Attempts to convert the string representation of a number to its <see cref="UInt256"/> equivalent.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 256-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse([NotNullWhen(true)] string? s, out UInt256 result)
        => TryParse(s, NumberStyles.Integer, CultureInfo.CurrentCulture, out result);

    /// <summary>
    /// Attempts to convert the string representation of a number in a specified style and culture-specific format to
    /// its <see cref="UInt256"/> equivalent.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 256-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(
        [NotNullWhen(true)] string? s,
        NumberStyles style,
        IFormatProvider? provider,
        out UInt256 result)
    {
        if (s is not null)
            return TryParseCore(s.AsSpan(), style, provider, out result);
        result = default;
        return false;
    }

    /// <summary>
    /// Attempts to convert the span representation of a number to its <see cref="UInt256"/> equivalent.
    /// </summary>
    /// <param name="s">A character span that contains the number to convert.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 256-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(ReadOnlySpan<char> s, out UInt256 result)
        => TryParse(s, NumberStyles.Integer, CultureInfo.CurrentCulture, out result);

    /// <summary>
    /// Attempts to convert the span representation of a number in a specified style and culture-specific format to its
    /// <see cref="UInt256"/> equivalent.
    /// </summary>
    /// <param name="s">A character span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 256-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(
        ReadOnlySpan<char> s,
        NumberStyles style,
        IFormatProvider? provider,
        out UInt256 result) =>
        TryParseCore(s, style, provider, out result);

    /// <summary>
    /// Attempts to convert the UTF-8 span representation of a number to its <see cref="UInt256"/> equivalent.
    /// </summary>
    /// <param name="utf8Text">A UTF-8 byte span that contains the number to convert.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 256-bit value represented by
    /// <paramref name="utf8Text"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(ReadOnlySpan<byte> utf8Text, out UInt256 result)
        => TryParse(utf8Text, NumberStyles.Integer, CultureInfo.CurrentCulture, out result);

    /// <summary>
    /// Attempts to convert the UTF-8 span representation of a number in a specified style and culture-specific format
    /// to its <see cref="UInt256"/> equivalent.
    /// </summary>
    /// <param name="utf8Text">A UTF-8 byte span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 256-bit value represented by
    /// <paramref name="utf8Text"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(
        ReadOnlySpan<byte> utf8Text,
        NumberStyles style,
        IFormatProvider? provider,
        out UInt256 result) =>
        NumericParseHelpers.TryParseUtf8(utf8Text, style, provider, TryParseCore, out result);

    #endregion

    #region Operators (unchecked)

    /// <summary>Determines whether two values are equal.</summary>
    /// <param name="left">The value on the left side of the equality comparison.</param>
    /// <param name="right">The value on the right side of the equality comparison.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator ==(UInt256 left, UInt256 right) => left.Equals(right);

    /// <summary>Determines whether two values are not equal.</summary>
    /// <param name="left">The value on the left side of the inequality comparison.</param>
    /// <param name="right">The value on the right side of the inequality comparison.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator !=(UInt256 left, UInt256 right) => !left.Equals(right);

    /// <summary>Determines whether one value is less than another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator <(UInt256 left, UInt256 right) => left.CompareTo(right) < 0;

    /// <summary>Determines whether one value is less than or equal to another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator <=(UInt256 left, UInt256 right) => left.CompareTo(right) <= 0;

    /// <summary>Determines whether one value is greater than another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator >(UInt256 left, UInt256 right) => left.CompareTo(right) > 0;

    /// <summary>Determines whether one value is greater than or equal to another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator >=(UInt256 left, UInt256 right) => left.CompareTo(right) >= 0;

    /// <summary>Adds two unsigned 256-bit values.</summary>
    /// <param name="left">The augend (the value to which <paramref name="right"/> is added).</param>
    /// <param name="right">The addend (the value added to <paramref name="left"/>).</param>
    public static UInt256 operator +(UInt256 left, UInt256 right)
    {
        UInt128 lo = left._lower + right._lower;
        return new(left._upper + right._upper + (UInt128)(lo < left._lower ? 1 : 0), lo);
    }

    /// <summary>Subtracts one unsigned 256-bit value from another.</summary>
    /// <param name="left">The minuend (the value from which <paramref name="right"/> is subtracted).</param>
    /// <param name="right">The subtrahend (the value subtracted from <paramref name="left"/>).</param>
    public static UInt256 operator -(UInt256 left, UInt256 right) =>
        new(
            left._upper - right._upper - (left._lower < right._lower ? (UInt128)1 : 0),
            left._lower - right._lower);

    /// <summary>Computes the bitwise AND of two values.</summary>
    /// <param name="left">The first bit pattern operand.</param>
    /// <param name="right">The second bit pattern operand.</param>
    public static UInt256 operator &(UInt256 left, UInt256 right) =>
        new(left._upper & right._upper, left._lower & right._lower);

    /// <summary>Computes the bitwise OR of two values.</summary>
    /// <param name="left">The first bit pattern operand.</param>
    /// <param name="right">The second bit pattern operand.</param>
    public static UInt256 operator |(UInt256 left, UInt256 right) =>
        new(left._upper | right._upper, left._lower | right._lower);

    /// <summary>Computes the bitwise exclusive OR of two values.</summary>
    /// <param name="left">The first bit pattern operand.</param>
    /// <param name="right">The second bit pattern operand.</param>
    public static UInt256 operator ^(UInt256 left, UInt256 right) =>
        new(left._upper ^ right._upper, left._lower ^ right._lower);

    /// <summary>Computes the bitwise complement of a value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt256 operator ~(UInt256 value) => new(~value._upper, ~value._lower);

    /// <summary>Returns the value unchanged.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt256 operator +(UInt256 value) => value;

    /// <summary>Increments a value by one.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt256 operator ++(UInt256 value) => value + One;

    /// <summary>Decrements a value by one.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt256 operator --(UInt256 value) => value - One;

    /// <summary>Shifts a value left by the specified number of bits.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="shiftAmount">The number of bit positions to shift.</param>
    public static UInt256 operator <<(UInt256 value, int shiftAmount)
    {
        shiftAmount &= 0xFF;
        return shiftAmount switch
        {
            0 => value,
            < 128 => new(
                (value._upper << shiftAmount) | (value._lower >> (128 - shiftAmount)),
                value._lower << shiftAmount),
            128 => new(value._lower, 0),
            _ => new(value._lower << (shiftAmount - 128), 0)
        };
    }

    /// <summary>Shifts a value right by the specified number of bits.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="shiftAmount">The number of bit positions to shift.</param>
    public static UInt256 operator >>(UInt256 value, int shiftAmount)
    {
        shiftAmount &= 0xFF;
        return shiftAmount switch
        {
            0 => value,
            < 128 => new(
                value._upper >> shiftAmount,
                (value._lower >> shiftAmount) | (value._upper << (128 - shiftAmount))),
            128 => new(0, value._upper),
            _ => new(0, value._upper >> (shiftAmount - 128))
        };
    }

    /// <summary>Shifts a value right by the specified number of bits with zero-fill semantics.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="shiftAmount">The number of bit positions to shift.</param>
    /// <remarks>
    /// For unsigned values, logical and arithmetic right shifts are equivalent; this operator forwards to
    /// <see cref="operator >>(UInt256, int)"/>.
    /// </remarks>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt256 operator >>>(UInt256 value, int shiftAmount) => value >> shiftAmount;

    /// <summary>Multiplies two unsigned 256-bit values.</summary>
    /// <param name="left">The multiplicand.</param>
    /// <param name="right">The multiplier.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt256 operator *(UInt256 left, UInt256 right) => Multiply(left, right, out _);

    /// <summary>Divides one unsigned 256-bit value by another.</summary>
    /// <param name="left">The dividend.</param>
    /// <param name="right">The divisor.</param>
    public static UInt256 operator /(UInt256 left, UInt256 right)
    {
        Divide(left, right, out UInt256 quotient, out _);
        return quotient;
    }

    /// <summary>Computes the remainder after dividing one unsigned 256-bit value by another.</summary>
    /// <param name="left">The dividend from which the remainder is computed.</param>
    /// <param name="right">The divisor used to compute the remainder.</param>
    public static UInt256 operator %(UInt256 left, UInt256 right)
    {
        Divide(left, right, out _, out UInt256 remainder);
        return remainder;
    }

    #endregion

    #region checked operators

    /// <summary>Adds two values and throws when the result is outside the <see cref="UInt256"/> range.</summary>
    /// <param name="left">The augend (the value to which <paramref name="right"/> is added).</param>
    /// <param name="right">The addend (the value added to <paramref name="left"/>).</param>
    /// <exception cref="OverflowException">The result cannot be represented as <see cref="UInt256"/>.</exception>
    public static UInt256 operator checked +(UInt256 left, UInt256 right)
    {
        UInt128 lo = left._lower + right._lower;
        UInt128 carry = lo < left._lower ? (UInt128)1 : 0;
        UInt128 hi = checked(left._upper + right._upper + carry);
        return new(hi, lo);
    }

    /// <summary>Subtracts one value from another and throws when the result would be negative.</summary>
    /// <param name="left">The minuend (the value from which <paramref name="right"/> is subtracted).</param>
    /// <param name="right">The subtrahend (the value subtracted from <paramref name="left"/>).</param>
    /// <exception cref="OverflowException">
    /// The subtraction result is outside the <see cref="UInt256"/> range.
    /// </exception>
    public static UInt256 operator checked -(UInt256 left, UInt256 right)
    {
        UInt128 lo = left._lower - right._lower;
        UInt128 borrow = lo > left._lower ? (UInt128)1 : 0;
        UInt128 hi = checked(left._upper - right._upper - borrow);
        return new(hi, lo);
    }

    /// <summary>Multiplies two values and throws when the result does not fit in 256 bits.</summary>
    /// <param name="left">The multiplicand.</param>
    /// <param name="right">The multiplier.</param>
    /// <exception cref="OverflowException">The product is outside the <see cref="UInt256"/> range.</exception>
    public static UInt256 operator checked *(UInt256 left, UInt256 right)
    {
        UInt256 product = Multiply(left, right, out bool overflow);
        return overflow
            ? throw new OverflowException()
            : product;
    }

    /// <summary>Divides one value by another in a checked context.</summary>
    /// <param name="left">The dividend.</param>
    /// <param name="right">The divisor.</param>
    /// <exception cref="DivideByZeroException"><paramref name="right"/> is zero.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt256 operator checked /(UInt256 left, UInt256 right) => left / right;

    /// <summary>Increments a value by one and throws when the input is <see cref="MaxValue"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is <see cref="MaxValue"/>.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt256 operator checked ++(UInt256 value) => checked(value + One);

    /// <summary>Decrements a value by one and throws when the input is <see cref="Zero"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is <see cref="Zero"/>.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt256 operator checked --(UInt256 value) => checked(value - One);

    #endregion

    #region Bit helpers

    /// <summary>Counts the number of leading zero bits in the specified value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int LeadingZeroCount(UInt256 value) =>
        value._upper == 0
            ? 128 + BitHelpers.LeadingZeroCount(value._lower)
            : BitHelpers.LeadingZeroCount(value._upper);

    /// <summary>Counts the number of trailing zero bits in the specified value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int TrailingZeroCount(UInt256 value) =>
        value._lower == 0
            ? 128 + BitHelpers.TrailingZeroCount(value._upper)
            : BitHelpers.TrailingZeroCount(value._lower);

    /// <summary>Gets the number of set bits in the specified value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int PopCount(UInt256 value)
        => BitHelpers.PopCount(value._lower) + BitHelpers.PopCount(value._upper);

    /// <summary>Rotates all bits in the value left by a specified amount.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="rotateAmount">The number of bit positions to rotate.</param>
    public static UInt256 RotateLeft(UInt256 value, int rotateAmount)
        => (value << rotateAmount) | (value >> (Bits - rotateAmount));

    /// <summary>Rotates all bits in the value right by a specified amount.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="rotateAmount">The number of bit positions to rotate.</param>
    public static UInt256 RotateRight(UInt256 value, int rotateAmount)
        => (value >> rotateAmount) | (value << (Bits - rotateAmount));

    /// <summary>Computes the integer base-2 logarithm of a value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int Log2(UInt256 value) =>
        value.IsZero ? 0 : Bits - 1 - LeadingZeroCount(value);

    #endregion

    #region Byte representation helpers

    /// <summary>
    /// Gets the length, in bits, of the shortest unsigned binary representation of the current value.
    /// Zero has a shortest bit length of zero, matching the <see cref="UInt128"/> integral convention.
    /// </summary>
    public int GetShortestBitLength() => IsZero ? 0 : Bits - LeadingZeroCount(this);

    /// <summary>
    /// Gets the fixed storage width of the current value in bytes.
    /// </summary>
    public int GetByteCount() => Bytes;

    /// <summary>
    /// Formats an unsigned 256-bit value using a supported standard numeric specifier and optional precision.
    /// </summary>
    /// <param name="value">The value to format.</param>
    /// <param name="format">
    /// The requested format. Supported standard specifiers are <c>G</c>/<c>g</c>, <c>D</c>/<c>d</c>,
    /// <c>N</c>/<c>n</c>, and <c>X</c>/<c>x</c>, optionally followed by a non-negative decimal precision.
    /// A <see langword="null"/> or empty value defaults to decimal formatting.
    /// </param>
    /// <param name="provider">
    /// The format provider used for culture-specific decimal digit formatting.
    /// </param>
    /// <returns>The formatted text representation of <paramref name="value"/>.</returns>
    /// <exception cref="FormatException">
    /// Thrown when <paramref name="format"/> is not a supported standard numeric format or has invalid precision.
    /// </exception>
    private static string FormatValue(UInt256 value, string? format, IFormatProvider? provider)
    {
        if (string.IsNullOrEmpty(format))
            return FormatDecimal(value, provider);

        char spec = format[0];
        return spec switch
        {
            'G' or 'g' or 'D' or 'd' when format.Length == 1 => FormatDecimal(value, provider),
            'G' or 'g' or 'D' or 'd' or 'N' or 'n' => value.ToBigInteger().ToString(format, provider),
            'X' or 'x' => NumericFormatHelpers.ApplyHexPrecision(FormatHex(value, spec == 'x'), format),
            _ => throw new FormatException()
        };
    }

    /// <summary>
    /// Formats an unsigned 256-bit value as base-10 digits.
    /// </summary>
    /// <param name="value">The value to format.</param>
    /// <param name="provider">
    /// The format provider used for culture-specific decimal digit formatting.
    /// </param>
    /// <returns>A decimal string that round-trips through <see cref="Parse(string)"/>.</returns>
    private static string FormatDecimal(UInt256 value, IFormatProvider? provider)
    {
        if (value.IsZero)
            return 0UL.ToString(provider);

        Span<UInt256> parts = stackalloc UInt256[14];
        int count = 0;
        UInt256 current = value;
        UInt256 divisor = new(0, 10_000_000_000_000_000_000UL);
        while (!current.IsZero)
        {
            Divide(current, divisor, out current, out UInt256 rem);
            parts[count++] = rem;
        }

        StringBuilder sb = new StringBuilder(count * 19);
        sb.Append(((ulong)parts[count - 1]).ToString(provider));
        for (int i = count - 2; i >= 0; i--)
            sb.Append(((ulong)parts[i]).ToString("D19", provider));
        return sb.ToString();
    }

    /// <summary>
    /// Formats an unsigned 256-bit value as hexadecimal without leading zero digits.
    /// </summary>
    /// <param name="value">The value to format.</param>
    /// <param name="lower">
    /// <see langword="true"/> to emit lowercase digits <c>a-f</c>; <see langword="false"/> to emit uppercase
    /// digits <c>A-F</c>.
    /// </param>
    /// <returns>The hexadecimal representation of <paramref name="value"/>.</returns>
    private static string FormatHex(UInt256 value, bool lower)
    {
        Span<byte> bytes = stackalloc byte[Bytes];
        WriteUInt128LittleEndian(value._lower, bytes[..16]);
        WriteUInt128LittleEndian(value._upper, bytes.Slice(16, 16));
        char a = lower ? 'a' : 'A';
        Span<char> chars = stackalloc char[64];
        int idx = 0;
        bool started = false;
        for (int i = Bytes - 1; i >= 0; i--)
        {
            byte b = bytes[i];
            int hi = b >> 4;
            int lo = b & 0xF;
            if (!started)
            {
                if (hi != 0)
                {
                    chars[idx++] = (char)(hi < 10 ? '0' + hi : a + (hi - 10));
                    started = true;
                }
                if (lo != 0 || started)
                {
                    chars[idx++] = (char)(lo < 10 ? '0' + lo : a + (lo - 10));
                    started = true;
                }
            }
            else
            {
                chars[idx++] = (char)(hi < 10 ? '0' + hi : a + (hi - 10));
                chars[idx++] = (char)(lo < 10 ? '0' + lo : a + (lo - 10));
            }
        }

        return idx == 0 ? "0" : new string(chars[..idx]);
    }

    /// <summary>
    /// Parses text into an unsigned 256-bit value and throws when parsing fails.
    /// </summary>
    /// <param name="text">The input text to parse.</param>
    /// <param name="style">The accepted style flags.</param>
    /// <param name="provider">
    /// The format provider that supplies culture-specific sign tokens used while parsing decimal input.
    /// </param>
    /// <returns>The parsed value.</returns>
    /// <exception cref="FormatException">
    /// Thrown when <paramref name="text"/> is syntactically invalid for the provided <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// Thrown when <paramref name="text"/> represents a numeric value larger than <see cref="MaxValue"/>.
    /// </exception>
    private static UInt256 ParseCore(ReadOnlySpan<char> text, NumberStyles style, IFormatProvider? provider) =>
        TryParseCore(text, style, provider, out UInt256 value, out bool overflow)
            ? value
            : throw (overflow
                ? new OverflowException()
                : new FormatException());

    /// <summary>
    /// Attempts to parse text into an unsigned 256-bit value.
    /// </summary>
    /// <param name="text">The input text to parse.</param>
    /// <param name="style">The accepted style flags.</param>
    /// <param name="provider">
    /// The format provider that supplies culture-specific sign tokens used while parsing decimal input.
    /// </param>
    /// <param name="value">
    /// When this method returns <see langword="true"/>, contains the parsed value; otherwise, contains
    /// <see cref="Zero"/>.
    /// </param>
    /// <returns><see langword="true"/> when parsing succeeds; otherwise, <see langword="false"/>.</returns>
    private static bool TryParseCore(ReadOnlySpan<char> text, NumberStyles style, IFormatProvider? provider, out UInt256 value)
        => TryParseCore(text, style, provider, out value, out _);

    /// <summary>
    /// Attempts to parse text into an unsigned 256-bit value and reports overflow separately from syntax failures.
    /// </summary>
    /// <param name="text">The input text to parse.</param>
    /// <param name="style">The accepted style flags.</param>
    /// <param name="provider">
    /// The format provider that supplies culture-specific sign tokens used while parsing decimal input.
    /// </param>
    /// <param name="value">
    /// When this method returns <see langword="true"/>, contains the parsed value; otherwise, contains
    /// <see cref="Zero"/>.
    /// </param>
    /// <param name="overflow">
    /// When this method returns <see langword="false"/>, indicates whether the failure occurred because the input was
    /// numerically out of range.
    /// </param>
    /// <returns><see langword="true"/> when parsing succeeds; otherwise, <see langword="false"/>.</returns>
    private static bool TryParseCore(ReadOnlySpan<char> text, NumberStyles style, IFormatProvider? provider, out UInt256 value, out bool overflow)
    {
        value = Zero;
        overflow = false;
        if ((style & NumberStyles.AllowHexSpecifier) != 0)
            return TryParseHex(text, style, out value, out overflow);

        if (!NumericParseHelpers.TryNormalizeDecimalText(text, style, out text) ||
            !BigInteger.TryParse(text, style, provider, out BigInteger parsed))
            return false;

        if (parsed.Sign < 0 || parsed > s_bigMaxValue)
        {
            overflow = true;
            return false;
        }

        value = (UInt256)parsed;
        return true;
    }

    /// <summary>
    /// Attempts to parse a hexadecimal representation into an unsigned 256-bit value.
    /// </summary>
    /// <param name="text">The hexadecimal input text.</param>
    /// <param name="style">
    /// The accepted hexadecimal style flags.
    /// </param>
    /// <param name="value">
    /// When this method returns <see langword="true"/>, contains the parsed value; otherwise, contains
    /// <see cref="Zero"/>.
    /// </param>
    /// <param name="overflow">
    /// When this method returns <see langword="false"/>, indicates whether the input exceeded 64 hexadecimal digits.
    /// </param>
    /// <returns><see langword="true"/> when parsing succeeds; otherwise, <see langword="false"/>.</returns>
    private static bool TryParseHex(ReadOnlySpan<char> text, NumberStyles style, out UInt256 value, out bool overflow)
    {
        value = Zero;
        overflow = false;
        if (!NumericParseHelpers.TryNormalizeHexText(text, style, out text))
            return false;
        if (text.IsEmpty)
            return false;

        // Redundant leading zeros do not count against the digit budget (BCL parity).
        text = text.TrimStart('0');
        if (text.IsEmpty)
            return true;

        if (text.Length > 64)
        {
            overflow = true;
            return false;
        }

        UInt256 result = Zero;
        foreach (char ch in text)
        {
            int d = ch switch
            {
                <= '9' and >= '0' => ch - '0',
                <= 'F' and >= 'A' => ch - 'A' + 10,
                <= 'f' and >= 'a' => ch - 'a' + 10,
                _ => -1
            };
            if (d < 0) return false;
            result = (result << 4) | new UInt256(0, (ulong)d);
        }

        value = result;
        return true;
    }

    /// <summary>
    /// Multiplies two unsigned 256-bit values and reports whether upper discarded bits were produced.
    /// </summary>
    /// <param name="left">The left multiplicand.</param>
    /// <param name="right">The right multiplicand.</param>
    /// <param name="overflow">
    /// Set to <see langword="true"/> when the full 512-bit product does not fit in 256 bits.
    /// </param>
    /// <returns>The low 256 bits of the product.</returns>
    private static UInt256 Multiply(UInt256 left, UInt256 right, out bool overflow)
    {
        Span<ulong> a = [
            (ulong)left._lower,
            (ulong)(left._lower >> 64),
            (ulong)left._upper,
            (ulong)(left._upper >> 64)
        ];
        Span<ulong> b = [
            (ulong)right._lower,
            (ulong)(right._lower >> 64),
            (ulong)right._upper,
            (ulong)(right._upper >> 64)
        ];
        Span<ulong> r = stackalloc ulong[8];

        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                UInt128 mul = (UInt128)a[i] * b[j];
                int k = i + j;

                UInt128 sum = (UInt128)r[k] + (ulong)mul;
                r[k] = (ulong)sum;

                ulong carry = (ulong)(sum >> 64) + (ulong)(mul >> 64);
                k++;

                while (carry != 0)
                {
                    sum = (UInt128)r[k] + carry;
                    r[k] = (ulong)sum;
                    carry = (ulong)(sum >> 64);
                    k++;
                }
            }
        }

        overflow = r[4] != 0 || r[5] != 0 || r[6] != 0 || r[7] != 0;
        return new(((UInt128)r[3] << 64) | r[2], ((UInt128)r[1] << 64) | r[0]);
    }

    /// <summary>
    /// Divides one unsigned 256-bit value by another using restoring binary long division.
    /// </summary>
    /// <param name="dividend">The value to divide.</param>
    /// <param name="divisor">The value that divides <paramref name="dividend"/>.</param>
    /// <param name="quotient">Receives the integer quotient.</param>
    /// <param name="remainder">Receives the non-negative remainder.</param>
    /// <exception cref="DivideByZeroException">
    /// Thrown before any quotient or remainder calculation when <paramref name="divisor"/> is zero.
    /// </exception>
    private static void Divide(UInt256 dividend, UInt256 divisor, out UInt256 quotient, out UInt256 remainder)
    {
        if (divisor.IsZero) throw new DivideByZeroException();
        if (dividend < divisor)
        {
            quotient = Zero;
            remainder = dividend;
            return;
        }

        quotient = Zero;
        remainder = dividend;
        int shift = LeadingZeroCount(divisor) - LeadingZeroCount(dividend);
        UInt256 scaled = divisor << shift;
        for (; shift >= 0; shift--)
        {
            if (remainder >= scaled)
            {
                remainder -= scaled;
                quotient |= One << shift;
            }
            scaled >>= 1;
        }
    }

    #endregion

    #region BigInteger interop

    /// <summary>
    /// Converts this value into an equivalent non-negative <see cref="BigInteger"/>.
    /// </summary>
    /// <returns>A <see cref="BigInteger"/> with the same unsigned 256-bit magnitude.</returns>
    private BigInteger ToBigInteger()
    {
        Span<byte> bytes = stackalloc byte[Bytes];
        bytes.Clear();
        WriteUInt128LittleEndian(_lower, bytes[..16]);
        WriteUInt128LittleEndian(_upper, bytes.Slice(16, 16));
        return new(bytes, isUnsigned: true, isBigEndian: false);
    }

    /// <summary>Converts a <see cref="UInt256"/> value to a <see cref="BigInteger"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator BigInteger(UInt256 value) => value.ToBigInteger();

    /// <summary>
    /// Converts a <see cref="BigInteger"/> to <see cref="UInt256"/> using unchecked wraparound semantics modulo 2^256.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator UInt256(BigInteger value)
    {
        // wraps (mod 2^256) for unchecked operations
        Span<byte> bytes = stackalloc byte[Bytes];
        bytes.Clear();

        // Signed two's-complement bytes let negative inputs wrap correctly when truncated to 256 bits.
        byte[] src = value.ToByteArray(isUnsigned: false, isBigEndian: false);
        int copy = Math.Min(src.Length, Bytes);
        src.AsSpan(0, copy).CopyTo(bytes);

        // Negative values shorter than 32 bytes require sign-extension so truncation still represents mod 2^256.
        if (value.Sign < 0 && copy < Bytes)
            bytes[copy..].Fill(0xFF);

        return ReadFullLittleEndian(bytes);
    }

    /// <summary>
    /// Converts a <see cref="BigInteger"/> to <see cref="UInt256"/> and throws when the source value is negative or
    /// outside the representable range.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">
    /// <paramref name="value"/> is negative or larger than <see cref="MaxValue"/>.
    /// </exception>
    public static explicit operator checked UInt256(BigInteger value)
    {
        if (value.Sign < 0 || value > s_bigMaxValue)
            throw new OverflowException();

        Span<byte> bytes = stackalloc byte[Bytes];
        bytes.Clear();
        byte[] src = value.ToByteArray(isUnsigned: true, isBigEndian: false);
        if (src.Length > Bytes)
            throw new OverflowException();

        src.AsSpan().CopyTo(bytes);
        return ReadFullLittleEndian(bytes);
    }

    /// <summary>Converts the value to double-precision floating point.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator double(UInt256 value) => (double)value.ToBigInteger();

    /// <summary>Converts the value to single-precision floating point.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator float(UInt256 value) => (float)value.ToBigInteger();

    /// <summary>Converts the value to decimal floating point.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is outside the <see cref="decimal"/> range.</exception>
    public static explicit operator decimal(UInt256 value) => (decimal)value.ToBigInteger();

    /// <summary>Converts a double-precision value by truncating and clamping to the unsigned range.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt256(double value) =>
        (UInt256)NumericConversionHelpers.FromFloatingPoint(value, BigInteger.Zero, s_bigMaxValue, isChecked: false);

    /// <summary>Converts a double-precision value and throws when it is outside the unsigned range.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is non-finite, negative, or too large.</exception>
    public static explicit operator checked UInt256(double value) =>
        (UInt256)NumericConversionHelpers.FromFloatingPoint(value, BigInteger.Zero, s_bigMaxValue, isChecked: true);

    /// <summary>Converts a single-precision value by truncating and clamping to the unsigned range.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt256(float value) => (UInt256)(double)value;

    /// <summary>Converts a single-precision value and throws when it is outside the unsigned range.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is non-finite, negative, or too large.</exception>
    public static explicit operator checked UInt256(float value) => checked((UInt256)(double)value);

    /// <summary>Converts a decimal value by truncating its fractional component.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator UInt256(decimal value) =>
        value < 0 ? throw new OverflowException() : (UInt256)(BigInteger)value;

    #endregion

    #region Conversions (built-in integral types)

    /// <summary>Converts an 8-bit unsigned value to <see cref="UInt256"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt256(byte value) => new(0, value);

    /// <summary>Converts a 16-bit unsigned value to <see cref="UInt256"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt256(ushort value) => new(0, value);

    /// <summary>Converts a 32-bit unsigned value to <see cref="UInt256"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt256(uint value) => new(0, value);

    /// <summary>Converts a 64-bit unsigned value to <see cref="UInt256"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt256(ulong value) => new(0, value);

    /// <summary>Converts a 128-bit unsigned value to <see cref="UInt256"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt256(UInt128 value) => new(0, value);

    /// <summary>Converts an 8-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt256(sbyte value) => (UInt256)(BigInteger)value;

    /// <summary>Converts an 8-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt256(sbyte value) => checked((UInt256)(BigInteger)value);

    /// <summary>Converts a 16-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt256(short value) => (UInt256)(BigInteger)value;

    /// <summary>Converts a 16-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt256(short value) => checked((UInt256)(BigInteger)value);

    /// <summary>Converts a 32-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt256(int value) => (UInt256)(BigInteger)value;

    /// <summary>Converts a 32-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt256(int value) => checked((UInt256)(BigInteger)value);

    /// <summary>Converts a 64-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt256(long value) => (UInt256)(BigInteger)value;

    /// <summary>Converts a 64-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt256(long value) => checked((UInt256)(BigInteger)value);

    /// <summary>Converts a 128-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt256(Int128 value) => (UInt256)(BigInteger)value;

    /// <summary>Converts a 128-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt256(Int128 value) => checked((UInt256)(BigInteger)value);

    /// <summary>Converts a signed 256-bit value to <see cref="UInt256"/> by reinterpreting its bit pattern.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator UInt256(Int256 value) => new(value.Upper, value.Lower);

    /// <summary>Converts a signed 256-bit value to <see cref="UInt256"/> in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt256(Int256 value) =>
        value.IsNegative
            ? throw new OverflowException()
            : new(value.Upper, value.Lower);

    /// <summary>Converts a <see cref="UInt256"/> value to an 8-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator byte(UInt256 value) => (byte)value._lower;

    /// <summary>Converts a <see cref="UInt256"/> value to an 8-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="byte"/>.</exception>
    public static explicit operator checked byte(UInt256 value) =>
        checked((byte)(UInt128)value);

    /// <summary>Converts a <see cref="UInt256"/> value to a 16-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator ushort(UInt256 value) => (ushort)value._lower;

    /// <summary>Converts a <see cref="UInt256"/> value to a 16-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="ushort"/>.</exception>
    public static explicit operator checked ushort(UInt256 value) =>
        checked((ushort)(UInt128)value);

    /// <summary>Converts a <see cref="UInt256"/> value to a 32-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator uint(UInt256 value) => (uint)value._lower;

    /// <summary>Converts a <see cref="UInt256"/> value to a 32-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="uint"/>.</exception>
    public static explicit operator checked uint(UInt256 value) =>
        checked((uint)(UInt128)value);

    /// <summary>Converts a <see cref="UInt256"/> value to a 64-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator ulong(UInt256 value) => (ulong)value._lower;

    /// <summary>Converts a <see cref="UInt256"/> value to a 64-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="ulong"/>.</exception>
    public static explicit operator checked ulong(UInt256 value) =>
        checked((ulong)(UInt128)value);

    /// <summary>
    /// Converts a <see cref="UInt256"/> value to a 128-bit unsigned integer by returning the low 128 bits.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator UInt128(UInt256 value) => value._lower;

    /// <summary>Converts a <see cref="UInt256"/> value to a 128-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="UInt128"/>.</exception>
    public static explicit operator checked UInt128(UInt256 value) =>
        value._upper == 0
            ? value._lower
            : throw new OverflowException();

    /// <summary>Converts a <see cref="UInt256"/> value to an 8-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator sbyte(UInt256 value) => (sbyte)value._lower;

    /// <summary>Converts a <see cref="UInt256"/> value to an 8-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="sbyte"/>.</exception>
    public static explicit operator checked sbyte(UInt256 value) => checked((sbyte)(ulong)value);

    /// <summary>Converts a <see cref="UInt256"/> value to a 16-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator short(UInt256 value) => (short)value._lower;

    /// <summary>Converts a <see cref="UInt256"/> value to a 16-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="short"/>.</exception>
    public static explicit operator checked short(UInt256 value) => checked((short)(ulong)value);

    /// <summary>Converts a <see cref="UInt256"/> value to a 32-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator int(UInt256 value) => (int)value._lower;

    /// <summary>Converts a <see cref="UInt256"/> value to a 32-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="int"/>.</exception>
    public static explicit operator checked int(UInt256 value) => checked((int)(ulong)value);

    /// <summary>Converts a <see cref="UInt256"/> value to a 64-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator long(UInt256 value) => (long)value._lower;

    /// <summary>Converts a <see cref="UInt256"/> value to a 64-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="long"/>.</exception>
    public static explicit operator checked long(UInt256 value) => checked((long)(ulong)value);

    /// <summary>
    /// Converts a <see cref="UInt256"/> value to a 128-bit signed integer by reinterpreting the low 128 bits.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator Int128(UInt256 value) => (Int128)value._lower;

    /// <summary>Converts a <see cref="UInt256"/> value to a 128-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="Int128"/>.</exception>
    public static explicit operator checked Int128(UInt256 value) => checked((Int128)(UInt128)value);

    #endregion

    #region Private endian helpers

    /// <summary>
    /// Reads a 256-bit unsigned integer from a 32-byte little-endian span.
    /// </summary>
    /// <param name="source">A 32-byte source span whose first byte is the least significant byte.</param>
    /// <returns>The decoded <see cref="UInt256"/> value.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static UInt256 ReadFullLittleEndian(ReadOnlySpan<byte> source) =>
        new(
            ReadUInt128LittleEndian(source.Slice(16, 16)),
            ReadUInt128LittleEndian(source[..16]));

    #endregion
}
