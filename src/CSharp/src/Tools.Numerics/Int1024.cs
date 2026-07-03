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
/// Represents a signed 1024-bit integer that follows two's-complement semantics.
/// </summary>
/// <remarks>
/// <para>
/// <see cref="Int1024"/> is designed as the signed companion to <see cref="UInt1024"/> and intentionally mirrors the
/// shape and usage patterns of built-in integral primitives. Caller-visible behavior is value-based, deterministic,
/// and aligned with standard .NET numeric expectations for operators, parsing, formatting, and comparison.
/// </para>
/// <para>
/// Storage uses two <see cref="UInt512"/> halves in little-endian-by-halves order: <c>_lower</c> carries bits 0..511
/// and <c>_upper</c> carries bits 512..1023, including the sign bit at position 1023. Checked operators enforce the
/// inclusive signed range <c>[-2^1023, 2^1023 - 1]</c>, while unchecked operators wrap modulo <c>2^1024</c> and
/// reinterpret the resulting bit pattern as signed.
/// </para>
/// <para>
/// Arithmetic operations are implemented over the two 512-bit halves (including explicit multi-limb
/// multiplication/division paths), while <see cref="BigInteger"/> interop is used for cross-type conversion
/// boundaries.
/// </para>
/// <para>
/// Unchecked arithmetic follows two's-complement wrap semantics, while checked operators and narrowing conversions
/// enforce range boundaries by throwing <see cref="OverflowException"/>.
/// </para>
/// <para>
    /// The implementation is intended to be aligned with <see cref="System.Int128"/> behavior. It should also remain reasonably symmetric with its unsigned
/// counterpart <see cref="UInt1024"/> (in this project) to preserve behavioral consistency.
/// </para>
/// </remarks>
[StructLayout(LayoutKind.Sequential)]
[DebuggerDisplay("{ToString(),nq}")]
public readonly struct Int1024 :
    IComparable,
    IComparable<Int1024>,
    IEquatable<Int1024>,
    ISpanFormattable
{
    /// <summary>Total bit width of the integer representation.</summary>
    private const int Bits = 1024;

    /// <summary>Total byte width of the integer representation.</summary>
    private const int Bytes = Bits / 8;

    /// <summary>Number of magnitude bits available in signed two's-complement representation.</summary>
    private const int MagnitudeBits = 1023;

    /// <summary>
    /// Cached inclusive lower bound used by checked <see cref="BigInteger"/> conversions.
    /// </summary>
    private static readonly BigInteger s_bigMinValue = -(BigInteger.One << MagnitudeBits);

    /// <summary>
    /// Cached inclusive upper bound used by checked <see cref="BigInteger"/> conversions.
    /// </summary>
    private static readonly BigInteger s_bigMaxValue = (BigInteger.One << MagnitudeBits) - BigInteger.One;

    /// <summary>The most significant 512 bits, including the sign bit at bit position 1023.</summary>
    private readonly UInt512 _upper;

    /// <summary>The least significant 512 bits of the value.</summary>
    private readonly UInt512 _lower;

    /// <summary>Bit mask that isolates the sign bit within the upper 512-bit half.</summary>
    private static readonly UInt512 s_signMaskUpper = (UInt512)1 << 511;

    /// <summary>Initializes a new <see cref="Int1024"/> from its upper and lower 512-bit halves.</summary>
    /// <param name="upper">The most significant 512 bits in two's-complement form.</param>
    /// <param name="lower">The least significant 512 bits.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Int1024(UInt512 upper, UInt512 lower) => (_upper, _lower) = (upper, lower);

    /// <summary>Initializes a new <see cref="Int1024"/> from a 64-bit signed integer.</summary>
    /// <param name="value">The initial numeric value.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Int1024(long value)
    {
        Int512 v = value;
        _lower = (UInt512)v;
        _upper = v < 0 ? UInt512.MaxValue : 0;
    }

    /// <summary>Gets the upper 512-bit half (including the sign bit).</summary>
    internal UInt512 Upper => _upper;

    /// <summary>Gets the lower 512-bit half.</summary>
    internal UInt512 Lower => _lower;

    /// <summary>Represents the additive identity (0).</summary>
    public static readonly Int1024 Zero = default;

    /// <summary>Represents the multiplicative identity (1).</summary>
    public static readonly Int1024 One = new(0, 1);

    /// <summary>Represents the smallest possible <see cref="Int1024"/> value.</summary>
    public static readonly Int1024 MinValue = new((UInt512)1 << 511, 0);

    /// <summary>Represents the largest possible <see cref="Int1024"/> value.</summary>
    public static readonly Int1024 MaxValue = new((UInt512)Int512.MaxValue, UInt512.MaxValue);

    /// <summary>Gets a value indicating whether the current value is equal to zero.</summary>
    public bool IsZero => _upper == 0 && _lower == 0;

    /// <summary>Gets a value indicating whether the current value is negative.</summary>
    public bool IsNegative => (_upper & s_signMaskUpper) != 0;

    /// <summary>Compares the current value with another signed 1024-bit value.</summary>
    /// <param name="other">The value to compare with this instance.</param>
    /// <returns>
    /// A signed integer that indicates the relative order of this value and <paramref name="other"/>.
    /// </returns>
    public int CompareTo(Int1024 other)
    {
        int c = ((Int512)_upper).CompareTo((Int512)other._upper);
        return c != 0 ? c : _lower.CompareTo(other._lower);
    }

    /// <summary>
    /// Compares this instance to a boxed value.
    /// </summary>
    /// <param name="obj">The boxed value to compare with this instance.</param>
    /// <returns>
    /// A signed integer that indicates the relative order of this instance and <paramref name="obj"/>.
    /// </returns>
    /// <exception cref="ArgumentException"><paramref name="obj"/> is not an <see cref="Int1024"/>.</exception>
    int IComparable.CompareTo(object? obj) =>
        obj switch
        {
            null => 1,
            Int1024 other => CompareTo(other),
            _ => throw new ArgumentException("Object must be an Int1024.")
        };

    /// <summary>Determines whether the current value equals another <see cref="Int1024"/> value.</summary>
    /// <param name="other">The value to compare with this instance.</param>
    /// <returns><see langword="true"/> when both halves match; otherwise, <see langword="false"/>.</returns>
    public bool Equals(Int1024 other) => (_upper, _lower) == (other._upper, other._lower);

    /// <inheritdoc/>
    public override bool Equals([NotNullWhen(true)] object? obj) => obj is Int1024 other && Equals(other);

    /// <inheritdoc/>
    public override int GetHashCode() => HashCode.Combine(_upper, _lower);

    /// <summary>Converts the numeric value to its equivalent string representation.</summary>
    public override string ToString() => FormatDecimal(this, provider: null);

    /// <summary>
    /// Converts the numeric value to its equivalent string representation using the specified format and
    /// culture-specific format information.
    /// </summary>
    /// <param name="format">
    /// A one-character standard numeric format specifier (for example <c>G</c>, <c>D</c>, or <c>X</c>) that selects the
    /// textual representation.
    /// </param>
    /// <param name="formatProvider">
    /// An object that supplies culture-specific formatting information used for decimal/general formatting tokens such
    /// as the negative sign.
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
    /// A one-character standard numeric format specifier (for example <c>G</c>, <c>D</c>, or <c>X</c>) that selects the
    /// textual representation.
    /// </param>
    /// <param name="provider">
    /// An object that supplies culture-specific formatting information used for decimal/general formatting tokens such
    /// as the negative sign.
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
    /// A one-character standard numeric format specifier (for example <c>G</c>, <c>D</c>, or <c>X</c>) that selects the
    /// textual representation.
    /// </param>
    /// <param name="provider">
    /// An object that supplies culture-specific formatting information used for decimal/general formatting tokens such
    /// as the negative sign.
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

    /// <summary>Converts the string representation of a number to its <see cref="Int1024"/> equivalent.</summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <returns>The parsed signed 1024-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="s"/> is <see langword="null"/>.</exception>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> does not represent a valid integer in the default style.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[-2^1023, 2^1023 - 1]</c>.
    /// </exception>
    public static Int1024 Parse(string s) => Parse(s, NumberStyles.Integer, CultureInfo.CurrentCulture);

    /// <summary>
    /// Converts the string representation of a number to its <see cref="Int1024"/> equivalent using the specified format
    /// provider.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed signed 1024-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="s"/> is <see langword="null"/>.</exception>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> does not represent a valid integer in the default style.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[-2^1023, 2^1023 - 1]</c>.
    /// </exception>
    public static Int1024 Parse(string s, IFormatProvider? provider) => Parse(s, NumberStyles.Integer, provider);

    /// <summary>
    /// Converts the string representation of a number in a specified style and culture-specific format to its <see cref="Int1024"/> equivalent.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed signed 1024-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="s"/> is <see langword="null"/>.</exception>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> is empty or does not represent a valid value under <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[-2^1023, 2^1023 - 1]</c>.
    /// </exception>
    public static Int1024 Parse(string s, NumberStyles style, IFormatProvider? provider = null)
    {
        ArgumentNullException.ThrowIfNull(s);
        return ParseCore(s.AsSpan(), style, provider);
    }

    /// <summary>
    /// Converts the span representation of a number in a specified style and culture-specific format to its <see cref="Int1024"/> equivalent.
    /// </summary>
    /// <param name="s">A character span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed signed 1024-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> is empty or does not represent a valid value under <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[-2^1023, 2^1023 - 1]</c>.
    /// </exception>
    public static Int1024 Parse(
        ReadOnlySpan<char> s,
        NumberStyles style = NumberStyles.Integer,
        IFormatProvider? provider = null)
    {
        return ParseCore(s, style, provider);
    }

    /// <summary>
    /// Converts the UTF-8 span representation of a number in a specified style and culture-specific format to its <see cref="Int1024"/> equivalent.
    /// </summary>
    /// <param name="utf8Text">A UTF-8 byte span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed signed 1024-bit value represented by <paramref name="utf8Text"/>.</returns>
    /// <exception cref="FormatException">
    /// <paramref name="utf8Text"/> is empty or does not represent a valid value under <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="utf8Text"/> represents a value outside the inclusive range <c>[-2^1023, 2^1023 - 1]</c>.
    /// </exception>
    public static Int1024 Parse(
        ReadOnlySpan<byte> utf8Text,
        NumberStyles style = NumberStyles.Integer,
        IFormatProvider? provider = null)
    {
        return ParseCore(Encoding.UTF8.GetString(utf8Text), style, provider);
    }

    /// <summary>
    /// Attempts to convert the string representation of a number to its <see cref="Int1024"/> equivalent.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed signed 1024-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse([NotNullWhen(true)] string? s, out Int1024 result)
        => TryParse(s, NumberStyles.Integer, CultureInfo.CurrentCulture, out result);

    /// <summary>
    /// Attempts to convert the string representation of a number in a specified style and culture-specific format to
    /// its <see cref="Int1024"/> equivalent.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed signed 1024-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(
        [NotNullWhen(true)] string? s,
        NumberStyles style,
        IFormatProvider? provider,
        out Int1024 result)
    {
        if (s is not null)
            return TryParseCore(s.AsSpan(), style, provider, out result);
        result = default;
        return false;
    }

    /// <summary>
    /// Attempts to convert the span representation of a number to its <see cref="Int1024"/> equivalent.
    /// </summary>
    /// <param name="s">A character span that contains the number to convert.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed signed 1024-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(ReadOnlySpan<char> s, out Int1024 result)
        => TryParse(s, NumberStyles.Integer, CultureInfo.CurrentCulture, out result);

    /// <summary>
    /// Attempts to convert the span representation of a number in a specified style and culture-specific format to its
    /// <see cref="Int1024"/> equivalent.
    /// </summary>
    /// <param name="s">A character span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed signed 1024-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(
        ReadOnlySpan<char> s,
        NumberStyles style,
        IFormatProvider? provider,
        out Int1024 result) => TryParseCore(s, style, provider, out result);

    /// <summary>
    /// Attempts to convert the UTF-8 span representation of a number to its <see cref="Int1024"/> equivalent.
    /// </summary>
    /// <param name="utf8Text">A UTF-8 byte span that contains the number to convert.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed signed 1024-bit value represented by
    /// <paramref name="utf8Text"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(ReadOnlySpan<byte> utf8Text, out Int1024 result)
        => TryParse(utf8Text, NumberStyles.Integer, CultureInfo.CurrentCulture, out result);

    /// <summary>
    /// Attempts to convert the UTF-8 span representation of a number in a specified style and culture-specific format
    /// to its <see cref="Int1024"/> equivalent.
    /// </summary>
    /// <param name="utf8Text">A UTF-8 byte span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed signed 1024-bit value represented by
    /// <paramref name="utf8Text"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(
        ReadOnlySpan<byte> utf8Text,
        NumberStyles style,
        IFormatProvider? provider,
        out Int1024 result) => TryParse(Encoding.UTF8.GetString(utf8Text), style, provider, out result);

    #endregion

    #region Operators (unchecked)

    /// <summary>Determines whether two values are equal.</summary>
    /// <param name="left">The value on the left side of the equality comparison.</param>
    /// <param name="right">The value on the right side of the equality comparison.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator ==(Int1024 left, Int1024 right) => left.Equals(right);

    /// <summary>Determines whether two values are not equal.</summary>
    /// <param name="left">The value on the left side of the inequality comparison.</param>
    /// <param name="right">The value on the right side of the inequality comparison.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator !=(Int1024 left, Int1024 right) => !left.Equals(right);

    /// <summary>Determines whether one value is less than another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator <(Int1024 left, Int1024 right) => left.CompareTo(right) < 0;

    /// <summary>Determines whether one value is less than or equal to another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator <=(Int1024 left, Int1024 right) => left.CompareTo(right) <= 0;

    /// <summary>Determines whether one value is greater than another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator >(Int1024 left, Int1024 right) => left.CompareTo(right) > 0;

    /// <summary>Determines whether one value is greater than or equal to another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator >=(Int1024 left, Int1024 right) => left.CompareTo(right) >= 0;

    /// <summary>Adds two signed 1024-bit values.</summary>
    /// <param name="left">The augend (the value to which <paramref name="right"/> is added).</param>
    /// <param name="right">The addend (the value added to <paramref name="left"/>).</param>
    public static Int1024 operator +(Int1024 left, Int1024 right)
    {
        UInt512 lo = left._lower + right._lower;
        return new(left._upper + right._upper + (UInt512)(lo < left._lower ? 1 : 0), lo);
    }

    /// <summary>Subtracts one signed 1024-bit value from another.</summary>
    /// <param name="left">The minuend (the value from which <paramref name="right"/> is subtracted).</param>
    /// <param name="right">The subtrahend (the value subtracted from <paramref name="left"/>).</param>
    public static Int1024 operator -(Int1024 left, Int1024 right) =>
        new(
            left._upper - right._upper - (left._lower < right._lower ? (UInt512)1 : 0),
            left._lower - right._lower);

    /// <summary>Computes the bitwise AND of two values.</summary>
    /// <param name="left">The first bit pattern operand.</param>
    /// <param name="right">The second bit pattern operand.</param>
    public static Int1024 operator &(Int1024 left, Int1024 right) =>
        new(left._upper & right._upper, left._lower & right._lower);

    /// <summary>Computes the bitwise OR of two values.</summary>
    /// <param name="left">The first bit pattern operand.</param>
    /// <param name="right">The second bit pattern operand.</param>
    public static Int1024 operator |(Int1024 left, Int1024 right) =>
        new(left._upper | right._upper, left._lower | right._lower);

    /// <summary>Computes the bitwise exclusive OR of two values.</summary>
    /// <param name="left">The first bit pattern operand.</param>
    /// <param name="right">The second bit pattern operand.</param>
    public static Int1024 operator ^(Int1024 left, Int1024 right) =>
        new(left._upper ^ right._upper, left._lower ^ right._lower);

    /// <summary>Computes the bitwise complement of a value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Int1024 operator ~(Int1024 value) => new(~value._upper, ~value._lower);

    /// <summary>Negates the specified value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Int1024 operator -(Int1024 value) => Zero - value;

    /// <summary>Returns the value unchanged.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Int1024 operator +(Int1024 value) => value;

    /// <summary>Increments a value by one.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Int1024 operator ++(Int1024 value) => value + One;

    /// <summary>Decrements a value by one.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Int1024 operator --(Int1024 value) => value - One;

    /// <summary>Shifts a value left by the specified number of bits.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="shiftAmount">The number of bit positions to shift.</param>
    public static Int1024 operator <<(Int1024 value, int shiftAmount)
    {
        shiftAmount &= 0x3FF;
        return shiftAmount switch
        {
            0 => value,
            < 512 => new(
                (value._upper << shiftAmount) | (value._lower >> (512 - shiftAmount)),
                value._lower << shiftAmount),
            512 => new(value._lower, 0),
            _ => new(value._lower << (shiftAmount - 512), 0)
        };
    }

    /// <summary>Shifts a value right using arithmetic sign extension.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="shiftAmount">The number of bit positions to shift.</param>
    public static Int1024 operator >>(Int1024 value, int shiftAmount)
    {
        shiftAmount &= 0x3FF;
        Int512 upperSigned = (Int512)value._upper;
        UInt512 fill = upperSigned < 0 ? UInt512.MaxValue : 0;

        return shiftAmount switch
        {
            0 => value,
            < 512 => new(
                (UInt512)(upperSigned >> shiftAmount),
                (value._lower >> shiftAmount) | (value._upper << (512 - shiftAmount))),
            512 => new(fill, (UInt512)upperSigned),
            _ => new(fill, (UInt512)(upperSigned >> (shiftAmount - 512)))
        };
    }

    /// <summary>Shifts a value right by the specified number of bits with zero-fill semantics.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="shiftAmount">The number of bit positions to shift.</param>
    /// <remarks>
    /// This operator performs a logical shift, unlike <see cref="operator >>(Int1024, int)"/> which preserves the
    /// sign bit through arithmetic extension.
    /// </remarks>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Int1024 operator >>>(Int1024 value, int shiftAmount)
        => (Int1024)((UInt1024)value >> shiftAmount);

    /// <summary>Multiplies two signed 1024-bit values.</summary>
    /// <param name="left">The multiplicand.</param>
    /// <param name="right">The multiplier.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Int1024 operator *(Int1024 left, Int1024 right) =>
        (Int1024)((UInt1024)left * (UInt1024)right);

    /// <summary>Divides one signed 1024-bit value by another.</summary>
    /// <param name="left">The dividend.</param>
    /// <param name="right">The divisor.</param>
    public static Int1024 operator /(Int1024 left, Int1024 right)
    {
        Divide(left, right, out Int1024 quotient, out _);
        return quotient;
    }

    /// <summary>Computes the remainder after dividing one signed 1024-bit value by another.</summary>
    /// <param name="left">The dividend from which the remainder is computed.</param>
    /// <param name="right">The divisor used to compute the remainder.</param>
    public static Int1024 operator %(Int1024 left, Int1024 right)
    {
        Divide(left, right, out _, out Int1024 remainder);
        return remainder;
    }

    #endregion

    #region checked operators

    /// <summary>Adds two values and throws when the result is outside the <see cref="Int1024"/> range.</summary>
    /// <param name="left">The augend (the value to which <paramref name="right"/> is added).</param>
    /// <param name="right">The addend (the value added to <paramref name="left"/>).</param>
    /// <exception cref="OverflowException">The result cannot be represented as <see cref="Int1024"/>.</exception>
    public static Int1024 operator checked +(Int1024 left, Int1024 right)
    {
        Int1024 sum = left + right;
        return (Int512)((sum._upper ^ left._upper) & ~(left._upper ^ right._upper)) < 0
            ? throw new OverflowException()
            : sum;
    }

    /// <summary>Subtracts two values and throws when the result is outside the <see cref="Int1024"/> range.</summary>
    /// <param name="left">The minuend (the value from which <paramref name="right"/> is subtracted).</param>
    /// <param name="right">The subtrahend (the value subtracted from <paramref name="left"/>).</param>
    /// <exception cref="OverflowException">The result cannot be represented as <see cref="Int1024"/>.</exception>
    public static Int1024 operator checked -(Int1024 left, Int1024 right)
    {
        Int1024 diff = left - right;
        return (Int512)((diff._upper ^ left._upper) & (left._upper ^ right._upper)) < 0
            ? throw new OverflowException()
            : diff;
    }

    /// <summary>Multiplies two values and throws when the product is outside the <see cref="Int1024"/> range.</summary>
    /// <param name="left">The multiplicand.</param>
    /// <param name="right">The multiplier.</param>
    /// <exception cref="OverflowException">The product cannot be represented as <see cref="Int1024"/>.</exception>
    public static Int1024 operator checked *(Int1024 left, Int1024 right)
    {
        Int1024 upper = BigMul(left, right, out Int1024 lower);
        return upper == (lower.IsNegative ? -One : Zero)
            ? lower
            : throw new OverflowException();
    }

    /// <summary>Divides one value by another in a checked context.</summary>
    /// <param name="left">The dividend.</param>
    /// <param name="right">The divisor.</param>
    /// <exception cref="DivideByZeroException"><paramref name="right"/> is zero.</exception>
    /// <exception cref="OverflowException">The operation is <see cref="MinValue"/> divided by <c>-1</c>.</exception>
    public static Int1024 operator checked /(Int1024 left, Int1024 right)
        => left == MinValue && right == -One
            ? throw new OverflowException()
            : left / right;

    /// <summary>Increments a value by one and throws when the input is <see cref="MaxValue"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is <see cref="MaxValue"/>.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Int1024 operator checked ++(Int1024 value) => checked(value + One);

    /// <summary>Decrements a value by one and throws when the input is <see cref="MinValue"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is <see cref="MinValue"/>.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Int1024 operator checked --(Int1024 value) => checked(value - One);

    /// <summary>Negates a value and throws when the input is <see cref="MinValue"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is <see cref="MinValue"/>.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Int1024 operator checked -(Int1024 value) => checked(Zero - value);

    #endregion

    #region Bit helpers

    /// <summary>Counts the number of leading zero bits in the two's-complement representation of a value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int LeadingZeroCount(Int1024 value) =>
        value._upper == 0
            ? 512 + BitHelpers.LeadingZeroCount(value._lower)
            : BitHelpers.LeadingZeroCount(value._upper);

    /// <summary>Counts the number of trailing zero bits in the two's-complement representation of a value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int TrailingZeroCount(Int1024 value) =>
        value._lower == 0
            ? 512 + BitHelpers.TrailingZeroCount(value._upper)
            : BitHelpers.TrailingZeroCount(value._lower);

    /// <summary>Gets the number of set bits in the specified value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int PopCount(Int1024 value)
        => BitHelpers.PopCount(value._lower) + BitHelpers.PopCount(value._upper);

    /// <summary>Rotates all bits in the value left by a specified amount.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="rotateAmount">The number of bit positions to rotate.</param>
    public static Int1024 RotateLeft(Int1024 value, int rotateAmount)
        => (Int1024)UInt1024.RotateLeft((UInt1024)value, rotateAmount);

    /// <summary>Rotates all bits in the value right by a specified amount.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="rotateAmount">The number of bit positions to rotate.</param>
    public static Int1024 RotateRight(Int1024 value, int rotateAmount)
        => (Int1024)UInt1024.RotateRight((UInt1024)value, rotateAmount);

    /// <summary>Computes the absolute value of a number.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static Int1024 Abs(Int1024 value) =>
        value == MinValue
            ? throw new OverflowException()
            : value.IsNegative ? -value : value;

    /// <summary>
    /// Gets the unsigned magnitude of a signed value without overflowing on <see cref="MinValue"/>.
    /// </summary>
    /// <param name="value">The signed value whose magnitude should be extracted.</param>
    /// <returns>
    /// A <see cref="UInt1024"/> that represents <c>|value|</c> interpreted in the unsigned 1024-bit domain.
    /// </returns>
    private static UInt1024 GetUnsignedMagnitude(Int1024 value)
    {
        UInt1024 bits = new(value._upper, value._lower);
        return value.IsNegative
            ? ~bits + UInt1024.One
            : bits;
    }

    /// <summary>
    /// Divides one signed 1024-bit value by another and returns both quotient and remainder.
    /// </summary>
    /// <param name="dividend">The value to divide.</param>
    /// <param name="divisor">The value to divide by.</param>
    /// <param name="quotient">When this method returns, receives the truncated quotient.</param>
    /// <param name="remainder">When this method returns, receives the remainder with the dividend sign.</param>
    /// <exception cref="DivideByZeroException"><paramref name="divisor"/> is zero.</exception>
    private static void Divide(Int1024 dividend, Int1024 divisor, out Int1024 quotient, out Int1024 remainder)
    {
        if (divisor.IsZero) throw new DivideByZeroException();

        UInt1024 left = GetUnsignedMagnitude(dividend);
        UInt1024 right = GetUnsignedMagnitude(divisor);

        quotient = (Int1024)(left / right);
        if (dividend.IsNegative ^ divisor.IsNegative)
            quotient = -quotient;

        remainder = (Int1024)(left % right);
        if (dividend.IsNegative)
            remainder = -remainder;
    }

    /// <summary>
    /// Produces the full signed product of two values and returns the upper 1024 bits.
    /// </summary>
    /// <param name="left">The first multiplicand.</param>
    /// <param name="right">The second multiplicand.</param>
    /// <param name="lower">Receives the low 1024 bits of the product.</param>
    /// <returns>The high 1024 bits of the 1024-bit signed product.</returns>
    private static Int1024 BigMul(Int1024 left, Int1024 right, out Int1024 lower)
    {
        UInt1024 upper = BigMulUnsigned((UInt1024)left, (UInt1024)right, out UInt1024 uLower);
        lower = (Int1024)uLower;
        return (Int1024)upper - ((left >> (Bits - 1)) & right) - ((right >> (Bits - 1)) & left);
    }

    /// <summary>
    /// Produces the full unsigned product of two values.
    /// </summary>
    /// <param name="left">The first unsigned multiplicand.</param>
    /// <param name="right">The second unsigned multiplicand.</param>
    /// <param name="lower">Receives the low 1024 bits of the product.</param>
    /// <returns>The high 1024 bits of the 1024-bit product.</returns>
    private static UInt1024 BigMulUnsigned(UInt1024 left, UInt1024 right, out UInt1024 lower)
    {
        Span<ulong> a =
        [
            (ulong)left.Lower,
            (ulong)(left.Lower >> 64),
            (ulong)(left.Lower >> 128),
            (ulong)(left.Lower >> 192),
            (ulong)(left.Lower >> 256),
            (ulong)(left.Lower >> 320),
            (ulong)(left.Lower >> 384),
            (ulong)(left.Lower >> 448),
            (ulong)left.Upper,
            (ulong)(left.Upper >> 64),
            (ulong)(left.Upper >> 128),
            (ulong)(left.Upper >> 192),
            (ulong)(left.Upper >> 256),
            (ulong)(left.Upper >> 320),
            (ulong)(left.Upper >> 384),
            (ulong)(left.Upper >> 448)
        ];
        Span<ulong> b =
        [
            (ulong)right.Lower,
            (ulong)(right.Lower >> 64),
            (ulong)(right.Lower >> 128),
            (ulong)(right.Lower >> 192),
            (ulong)(right.Lower >> 256),
            (ulong)(right.Lower >> 320),
            (ulong)(right.Lower >> 384),
            (ulong)(right.Lower >> 448),
            (ulong)right.Upper,
            (ulong)(right.Upper >> 64),
            (ulong)(right.Upper >> 128),
            (ulong)(right.Upper >> 192),
            (ulong)(right.Upper >> 256),
            (ulong)(right.Upper >> 320),
            (ulong)(right.Upper >> 384),
            (ulong)(right.Upper >> 448)
        ];

        Span<ulong> product = stackalloc ulong[32];
        for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
        {
            UInt128 multiplication = (UInt128)a[i] * b[j];
            int index = i + j;

            UInt128 sum = (UInt128)product[index] + (ulong)multiplication;
            product[index] = (ulong)sum;

            ulong carry = (ulong)(sum >> 64) + (ulong)(multiplication >> 64);
            index++;

            while (carry != 0)
            {
                sum = (UInt128)product[index] + carry;
                product[index] = (ulong)sum;
                carry = (ulong)(sum >> 64);
                index++;
            }
        }

        UInt512 lowerLower =
            ((UInt512)product[0]) |
            ((UInt512)product[1] << 64) |
            ((UInt512)product[2] << 128) |
            ((UInt512)product[3] << 192) |
            ((UInt512)product[4] << 256) |
            ((UInt512)product[5] << 320) |
            ((UInt512)product[6] << 384) |
            ((UInt512)product[7] << 448);
        UInt512 lowerUpper =
            ((UInt512)product[8]) |
            ((UInt512)product[9] << 64) |
            ((UInt512)product[10] << 128) |
            ((UInt512)product[11] << 192) |
            ((UInt512)product[12] << 256) |
            ((UInt512)product[13] << 320) |
            ((UInt512)product[14] << 384) |
            ((UInt512)product[15] << 448);
        UInt512 upperLower =
            ((UInt512)product[16]) |
            ((UInt512)product[17] << 64) |
            ((UInt512)product[18] << 128) |
            ((UInt512)product[19] << 192) |
            ((UInt512)product[20] << 256) |
            ((UInt512)product[21] << 320) |
            ((UInt512)product[22] << 384) |
            ((UInt512)product[23] << 448);
        UInt512 upperUpper =
            ((UInt512)product[24]) |
            ((UInt512)product[25] << 64) |
            ((UInt512)product[26] << 128) |
            ((UInt512)product[27] << 192) |
            ((UInt512)product[28] << 256) |
            ((UInt512)product[29] << 320) |
            ((UInt512)product[30] << 384) |
            ((UInt512)product[31] << 448);

        lower = new(lowerUpper, lowerLower);
        return new(upperUpper, upperLower);
    }

    /// <summary>Returns an integer that indicates the sign of the specified value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int Sign(Int1024 value) =>
        value.IsZero
            ? 0
            : value.IsNegative ? -1 : 1;

    #endregion

    #region Byte representation helpers

    /// <summary>
    /// Gets the length, in bits, of the shortest two's-complement representation of the current value.
    /// </summary>
    public int GetShortestBitLength() =>
        IsZero
            ? 1
            : IsNegative
                ? Bits + 1 - UInt1024.LeadingZeroCount(new UInt1024(~_upper, ~_lower))
                : Bits - UInt1024.LeadingZeroCount(new UInt1024(_upper, _lower)) + 1;

    /// <summary>
    /// Gets the number of bytes required by the shortest two's-complement representation of the current value.
    /// </summary>
    public int GetByteCount() => (GetShortestBitLength() + 7) / 8;

    /// <summary>
    /// Formats a signed 1024-bit value according to the supported one-character numeric format specifiers.
    /// </summary>
    /// <param name="value">The value to format.</param>
    /// <param name="format">
    /// The requested format specifier. Supported values are <c>G</c>/<c>g</c>, <c>D</c>/<c>d</c>,
    /// and <c>X</c>/<c>x</c>. A <see langword="null"/> or empty value defaults to decimal formatting.
    /// </param>
    /// <param name="provider">
    /// The format provider used for culture-specific decimal sign and digit formatting.
    /// </param>
    /// <returns>The formatted text representation of <paramref name="value"/>.</returns>
    /// <exception cref="FormatException">
    /// Thrown when <paramref name="format"/> is not one of the supported one-character specifiers.
    /// </exception>
    private static string FormatValue(Int1024 value, string? format, IFormatProvider? provider) =>
        string.IsNullOrEmpty(format)
            ? FormatDecimal(value, provider)
            : format[0] switch
            {
                'G' or 'g' or 'D' or 'd' when format.Length == 1 => FormatDecimal(value, provider),
                'X' or 'x' when format.Length == 1 => ((UInt1024)value).ToString(format, provider),
                _ => throw new FormatException()
            };

    /// <summary>
    /// Formats a signed 1024-bit value as base-10 digits.
    /// </summary>
    /// <param name="value">The value to format.</param>
    /// <param name="provider">
    /// The format provider used for culture-specific decimal sign and digit formatting.
    /// </param>
    /// <returns>A decimal string that round-trips through <see cref="Parse(string)"/>.</returns>
    private static string FormatDecimal(Int1024 value, IFormatProvider? provider)
    {
        NumberFormatInfo nfi = NumberFormatInfo.GetInstance(provider);

        if (!value.IsNegative)
            return ((UInt1024)value).ToString(null, provider);

        UInt1024 magnitude = value == MinValue ? (UInt1024)MinValue : (UInt1024)(-value);
        return nfi.NegativeSign + magnitude.ToString(null, provider);
    }

    /// <summary>
    /// Parses text into a signed 1024-bit value and throws when parsing fails.
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
    /// Thrown when <paramref name="text"/> represents a numeric value outside the <see cref="Int1024"/> range.
    /// </exception>
    private static Int1024 ParseCore(ReadOnlySpan<char> text, NumberStyles style, IFormatProvider? provider) =>
        TryParseCore(text, style, provider, out Int1024 value, out bool overflow)
            ? value
            : throw (overflow
                ? new OverflowException()
                : new FormatException());

    /// <summary>
    /// Attempts to parse text into a signed 1024-bit value.
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
    private static bool TryParseCore(ReadOnlySpan<char> text, NumberStyles style, IFormatProvider? provider, out Int1024 value)
        => TryParseCore(text, style, provider, out value, out _);

    /// <summary>
    /// Attempts to parse text into a signed 1024-bit value and reports overflow separately from syntax failures.
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
    private static bool TryParseCore(
        ReadOnlySpan<char> text,
        NumberStyles style,
        IFormatProvider? provider,
        out Int1024 value,
        out bool overflow)
    {
        value = Zero;
        overflow = false;
        if ((style & NumberStyles.AllowHexSpecifier) != 0)
            return TryParseHex(text, style, out value, out overflow);
        if (!NumericParseHelpers.TryNormalizeDecimalText(text, style, out text))
            return false;

        if (text.IsEmpty) return false;
        bool neg = false;

        if ((style & NumberStyles.AllowLeadingSign) != 0 &&
            NumericParseHelpers.TryStripLeadingSign(
                text,
                NumberFormatInfo.GetInstance(provider),
                out ReadOnlySpan<char> unsigned,
                out bool isNegative))
        {
            neg = isNegative;
            text = unsigned;
        }

        if (text.IsEmpty) return false;
        if (!UInt1024.TryParse(text, NumberStyles.None, provider, out UInt1024 u))
        {
            overflow = IsAllDecimalDigits(text);
            return false;
        }

        if (!neg)
        {
            if (u > (UInt1024)MaxValue)
            {
                overflow = true;
                return false;
            }
            value = (Int1024)u;
            return true;
        }

        if (u > (UInt1024)MaxValue + UInt1024.One)
        {
            overflow = true;
            return false;
        }

        value = -(Int1024)u;
        return true;
    }

    /// <summary>
    /// Determines whether a character span contains at least one character and every character is a decimal digit.
    /// </summary>
    /// <param name="text">The text to inspect.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="text"/> is non-empty and contains only <c>0</c> through <c>9</c>;
    /// otherwise, <see langword="false"/>.
    /// </returns>
    private static bool IsAllDecimalDigits(ReadOnlySpan<char> text)
    {
        foreach (char ch in text)
        {
            if ((uint)(ch - '0') > 9)
                return false;
        }

        return !text.IsEmpty;
    }

    /// <summary>
    /// Attempts to parse a hexadecimal representation into a signed 1024-bit value.
    /// </summary>
    /// <param name="text">The hexadecimal input text.</param>
    /// <param name="style">
    /// The accepted style flags, which must only include <see cref="NumberStyles.AllowHexSpecifier"/>.
    /// </param>
    /// <param name="value">
    /// When this method returns <see langword="true"/>, contains the parsed bit pattern reinterpreted as two's
    /// complement; otherwise, contains <see cref="Zero"/>.
    /// </param>
    /// <param name="overflow">
    /// When this method returns <see langword="false"/>, indicates whether the input exceeded the maximum
    /// hexadecimal digit count representable by this type.
    /// </param>
    /// <returns><see langword="true"/> when parsing succeeds; otherwise, <see langword="false"/>.</returns>
    private static bool TryParseHex(ReadOnlySpan<char> text, NumberStyles style, out Int1024 value, out bool overflow)
    {
        value = Zero;
        overflow = false;
        if (!NumericParseHelpers.TryNormalizeHexText(text, style, out text))
            return false;
        if (text.IsEmpty) return false;
        if (text.Length > 256)
        {
            overflow = true;
            return false;
        }

        UInt1024 bits = UInt1024.Zero;
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
            bits = (bits << 4) | (ulong)d;
        }

        value = (Int1024)bits;
        return true;
    }

    #endregion

    #region BigInteger interop

    /// <summary>
    /// Converts this value into an equivalent <see cref="BigInteger"/>.
    /// </summary>
    /// <returns>A signed <see cref="BigInteger"/> that preserves the exact two's-complement bit pattern.</returns>
    private BigInteger ToBigInteger()
    {
        Span<byte> bytes = stackalloc byte[Bytes];
        bytes.Clear();
        WriteUInt512LittleEndian(_lower, bytes[..64]);
        WriteUInt512LittleEndian(_upper, bytes.Slice(64, 64));
        return new(bytes, isUnsigned: false, isBigEndian: false);
    }

    /// <summary>Converts a <see cref="Int1024"/> value to a <see cref="BigInteger"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator BigInteger(Int1024 value) => value.ToBigInteger();

    /// <summary>
    /// Converts a <see cref="BigInteger"/> to <see cref="Int1024"/> using unchecked wraparound semantics modulo 2^1024.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator Int1024(BigInteger value)
    {
        // wraps (mod 2^1024) for unchecked operations
        Span<byte> bytes = stackalloc byte[Bytes];
        bytes.Clear();

        // For signed BigInteger, ToByteArray returns two's complement.
        byte[] src = value.ToByteArray(isUnsigned: false, isBigEndian: false);
        int copy = Math.Min(src.Length, Bytes);
        src.AsSpan(0, copy).CopyTo(bytes);

        // If src shorter than 64 and negative, we must sign-extend.
        if (value.Sign < 0 && copy < Bytes)
            bytes[copy..].Fill(0xFF);

        return ReadFullLittleEndian(bytes);
    }

    /// <summary>
    /// Converts a <see cref="BigInteger"/> to <see cref="Int1024"/> and throws when the source value is outside the
    /// representable range.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">
    /// <paramref name="value"/> is outside the <see cref="Int1024"/> range.
    /// </exception>
    public static explicit operator checked Int1024(BigInteger value)
    {
        if (value < s_bigMinValue || value > s_bigMaxValue)
            throw new OverflowException();

        Span<byte> bytes = stackalloc byte[Bytes];
        bytes.Clear();

        byte[] src = value.ToByteArray(isUnsigned: false, isBigEndian: false);
        if (src.Length > Bytes)
        {
            // BigInteger can include a sign byte; but since we've range-checked,
            // it must either fit in 64 or be a redundant sign extension.
            // Accept only redundant extension.
            byte ext = value.Sign < 0 ? (byte)0xFF : (byte)0x00;

            for (int i = src.Length - 1; i >= Bytes; i--)
            {
                if (src[i] == ext) continue;
                throw new OverflowException();
            }

            src = src.AsSpan(0, Bytes).ToArray();
        }

        src.AsSpan().CopyTo(bytes);

        // Sign-extend if needed.
        if (value.Sign < 0 && src.Length < Bytes)
            bytes[src.Length..].Fill(0xFF);

        return ReadFullLittleEndian(bytes);
    }

    #endregion

    #region Conversions (built-in integral types)

    /// <summary>Converts an 8-bit signed value to <see cref="Int1024"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator Int1024(sbyte value) => new(value);

    /// <summary>Converts a 16-bit signed value to <see cref="Int1024"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator Int1024(short value) => new(value);

    /// <summary>Converts a 32-bit signed value to <see cref="Int1024"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator Int1024(int value) => new(value);

    /// <summary>Converts a 64-bit signed value to <see cref="Int1024"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator Int1024(long value) => new(value);

    /// <summary>Converts an 8-bit unsigned value to <see cref="Int1024"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator Int1024(byte value) => new(0, value);

    /// <summary>Converts a 16-bit unsigned value to <see cref="Int1024"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator Int1024(ushort value) => new(0, value);

    /// <summary>Converts a 32-bit unsigned value to <see cref="Int1024"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator Int1024(uint value) => new(0, value);

    /// <summary>Converts a 64-bit unsigned value to <see cref="Int1024"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator Int1024(ulong value) => new(0, value);

    /// <summary>Converts a 512-bit signed value to <see cref="Int1024"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator Int1024(Int512 value) => new(value < 0 ? UInt512.MaxValue : 0, (UInt512)value);

    /// <summary>Converts a 512-bit unsigned value to <see cref="Int1024"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator Int1024(UInt512 value) => new(0, value);

    /// <summary>Converts an unsigned 1024-bit value to <see cref="Int1024"/> by reinterpreting its bit pattern.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator Int1024(UInt1024 value) => new(value.Upper, value.Lower);

    /// <summary>Converts an unsigned 1024-bit value to <see cref="Int1024"/> in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is greater than <see cref="MaxValue"/>.</exception>
    public static explicit operator checked Int1024(UInt1024 value) =>
        value.Upper > (UInt512)Int512.MaxValue
            ? throw new OverflowException()
            : new(value.Upper, value.Lower);

    /// <summary>Converts a <see cref="Int1024"/> value to an 8-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator byte(Int1024 value) => (byte)value._lower;

    /// <summary>Converts a <see cref="Int1024"/> value to an 8-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="byte"/>.</exception>
    public static explicit operator checked byte(Int1024 value) => checked((byte)(ulong)value);

    /// <summary>Converts a <see cref="Int1024"/> value to a 16-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator ushort(Int1024 value) => (ushort)value._lower;

    /// <summary>Converts a <see cref="Int1024"/> value to a 16-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="ushort"/>.</exception>
    public static explicit operator checked ushort(Int1024 value) => checked((ushort)(ulong)value);

    /// <summary>Converts a <see cref="Int1024"/> value to a 32-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator uint(Int1024 value) => (uint)value._lower;

    /// <summary>Converts a <see cref="Int1024"/> value to a 32-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="uint"/>.</exception>
    public static explicit operator checked uint(Int1024 value) => checked((uint)(ulong)value);

    /// <summary>Converts a <see cref="Int1024"/> value to a 64-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator ulong(Int1024 value) => (ulong)value._lower;

    /// <summary>Converts a <see cref="Int1024"/> value to a 64-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="ulong"/>.</exception>
    public static explicit operator checked ulong(Int1024 value) =>
        checked((ulong)(UInt512)value);

    /// <summary>
    /// Converts a <see cref="Int1024"/> value to a 512-bit unsigned integer by returning the low 512 bits.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator UInt512(Int1024 value) => value._lower;

    /// <summary>Converts a <see cref="Int1024"/> value to a 512-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="UInt512"/>.</exception>
    public static explicit operator checked UInt512(Int1024 value) =>
        value._upper == 0
            ? value._lower
            : throw new OverflowException();

    /// <summary>Converts a <see cref="Int1024"/> value to an 8-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator sbyte(Int1024 value) => (sbyte)value._lower;

    /// <summary>Converts a <see cref="Int1024"/> value to an 8-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="sbyte"/>.</exception>
    public static explicit operator checked sbyte(Int1024 value) => checked((sbyte)(long)value);

    /// <summary>Converts a <see cref="Int1024"/> value to a 16-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator short(Int1024 value) => (short)value._lower;

    /// <summary>Converts a <see cref="Int1024"/> value to a 16-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="short"/>.</exception>
    public static explicit operator checked short(Int1024 value) => checked((short)(long)value);

    /// <summary>Converts a <see cref="Int1024"/> value to a 32-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator int(Int1024 value) => (int)value._lower;

    /// <summary>Converts a <see cref="Int1024"/> value to a 32-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="int"/>.</exception>
    public static explicit operator checked int(Int1024 value) => checked((int)(long)value);

    /// <summary>Converts a <see cref="Int1024"/> value to a 64-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator long(Int1024 value) => (long)value._lower;

    /// <summary>Converts a <see cref="Int1024"/> value to a 64-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="long"/>.</exception>
    public static explicit operator checked long(Int1024 value)
        => checked((long)(Int512)value);

    /// <summary>
    /// Converts a <see cref="Int1024"/> value to a 512-bit signed integer by returning the low 512 bits.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator Int512(Int1024 value) => (Int512)value._lower;

    /// <summary>Converts a <see cref="Int1024"/> value to a 512-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="Int512"/>.</exception>
    public static explicit operator checked Int512(Int1024 value)
    {
        Int512 lower = (Int512)value._lower;
        return value._upper == (lower < 0 ? UInt512.MaxValue : 0)
            ? lower
            : throw new OverflowException();
    }

    #endregion

    #region Private endian helpers

    /// <summary>
    /// Reads a 1024-bit signed integer from a 128-byte little-endian span.
    /// </summary>
    /// <param name="source">A 128-byte source span whose first byte is the least significant byte.</param>
    /// <returns>The decoded <see cref="Int1024"/> value.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static Int1024 ReadFullLittleEndian(ReadOnlySpan<byte> source) =>
        new(
            ReadUInt512LittleEndian(source.Slice(64, 64)),
            ReadUInt512LittleEndian(source[..64]));

    #endregion
}
