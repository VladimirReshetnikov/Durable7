// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using static Tools.Numerics.BitHelpers;

namespace Tools.Numerics;

/// <summary>
/// Represents an unsigned 512-bit integer with deterministic arithmetic, parsing, formatting, and endian semantics.
/// </summary>
/// <remarks>
/// <para>
/// The value is represented by two <see cref="UInt256"/> halves. Arithmetic and bitwise operations are implemented
/// directly over those halves, including multiplication/division/modulus and decimal/hex text conversion paths.
/// </para>
/// <para>
/// The binary layout is little-endian by halves: <c>_lower</c> stores bits 0..255 and <c>_upper</c> stores bits
/// 256..511.
/// </para>
/// <para>
/// This type is intentionally designed to mirror the ergonomics of built-in integer primitives: operators are value-
/// based, parsing and formatting follow .NET numeric conventions, and endian helpers encode a canonical 64-byte wire
/// representation suitable for deterministic persistence and protocol payloads.
/// </para>
/// <para>
    /// The implementation is intended to be aligned with <see cref="System.UInt128"/> behavior. It should also be reasonably symmetric with its signed
/// counterpart <see cref="Int512"/> (in this project).
/// </para>
/// </remarks>
[StructLayout(LayoutKind.Sequential)]
[DebuggerDisplay("{ToString(),nq}")]
public readonly struct UInt512 :
    IComparable,
    IComparable<UInt512>,
    IEquatable<UInt512>,
    ISpanFormattable,
    IUtf8SpanFormattable,
    IParsable<UInt512>,
    ISpanParsable<UInt512>,
    IMinMaxValue<UInt512>
{
    /// <summary>Total bit width of the unsigned representation.</summary>
    private const int Bits = 512;

    /// <summary>Total byte width of the unsigned representation.</summary>
    private const int Bytes = Bits / 8;

    /// <summary>
    /// Cached inclusive upper bound used by checked <see cref="BigInteger"/> conversions.
    /// </summary>
    private static readonly BigInteger s_bigMaxValue = (BigInteger.One << Bits) - BigInteger.One;

    /// <summary>The most significant 256 bits (bit positions 256 through 511).</summary>
    private readonly UInt256 _upper; // bits 256..511

    /// <summary>The least significant 256 bits (bit positions 0 through 255).</summary>
    private readonly UInt256 _lower; // bits 0..255

    /// <summary>Initializes a new <see cref="UInt512"/> from its upper and lower 256-bit halves.</summary>
    /// <param name="upper">The most significant 256 bits (bits 256 through 511).</param>
    /// <param name="lower">The least significant 256 bits (bits 0 through 255).</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public UInt512(UInt256 upper, UInt256 lower) => (_upper, _lower) = (upper, lower);

    /// <summary>Initializes a new <see cref="UInt512"/> from a 64-bit value.</summary>
    /// <param name="value">The initial numeric value.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public UInt512(ulong value) => (_upper, _lower) = (0, value);

    /// <summary>Gets the upper 256-bit half (bits 256 through 511).</summary>
    internal UInt256 Upper => _upper;

    /// <summary>Gets the lower 256-bit half (bits 0 through 255).</summary>
    internal UInt256 Lower => _lower;

    /// <summary>Represents the additive identity (0).</summary>
    public static readonly UInt512 Zero = default;

    /// <summary>Represents the multiplicative identity (1).</summary>
    public static readonly UInt512 One = new(0, 1);

    /// <summary>Represents the smallest possible <see cref="UInt512"/> value.</summary>
    public static readonly UInt512 MinValue = Zero;

    /// <summary>Represents the largest possible <see cref="UInt512"/> value.</summary>
    public static readonly UInt512 MaxValue = new(UInt256.MaxValue, UInt256.MaxValue);

    static UInt512 IMinMaxValue<UInt512>.MinValue => MinValue;

    static UInt512 IMinMaxValue<UInt512>.MaxValue => MaxValue;

    static UInt512 IParsable<UInt512>.Parse(string s, IFormatProvider? provider) => Parse(s, provider);

    static bool IParsable<UInt512>.TryParse(string? s, IFormatProvider? provider, out UInt512 result) =>
        TryParse(s, NumberStyles.Integer, provider, out result);

    static UInt512 ISpanParsable<UInt512>.Parse(ReadOnlySpan<char> s, IFormatProvider? provider) =>
        Parse(s, NumberStyles.Integer, provider);

    static bool ISpanParsable<UInt512>.TryParse(ReadOnlySpan<char> s, IFormatProvider? provider, out UInt512 result) =>
        TryParse(s, NumberStyles.Integer, provider, out result);

    /// <summary>Gets a value indicating whether the current value is equal to zero.</summary>
    public bool IsZero => _upper == 0 && _lower == 0;

    /// <summary>Compares the current value with another value of the same type.</summary>
    /// <param name="other">The value to compare with this instance.</param>
    /// <returns>
    /// A signed integer that indicates the relative order of this instance and <paramref name="other"/>.
    /// </returns>
    public int CompareTo(UInt512 other)
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
    /// <exception cref="ArgumentException"><paramref name="obj"/> is not a <see cref="UInt512"/>.</exception>
    int IComparable.CompareTo(object? obj) =>
        obj switch
        {
            null => 1,
            UInt512 other => CompareTo(other),
            _ => throw new ArgumentException("Object must be a UInt512.")
        };

    /// <summary>Determines whether the current value is equal to another <see cref="UInt512"/> value.</summary>
    /// <param name="other">The value to compare with this instance.</param>
    /// <returns>
    /// <see langword="true"/> when both 256-bit halves are equal; otherwise, <see langword="false"/>.
    /// </returns>
    public bool Equals(UInt512 other) => (_upper, _lower) == (other._upper, other._lower);

    /// <summary>Determines whether the current value is equal to a specified object.</summary>
    /// <param name="obj">The object to compare with this instance.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="obj"/> is a <see cref="UInt512"/> with the same value.
    /// </returns>
    public override bool Equals([NotNullWhen(true)] object? obj) => obj is UInt512 other && Equals(other);

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
    /// A standard numeric format specifier that selects the textual representation. <c>D</c>, <c>N</c>, and
    /// <c>X</c> accept an optional precision (for example <c>D5</c>, <c>N0</c>, or <c>X8</c>); <c>G</c> accepts
    /// no precision and rejects a precision specifier with <see cref="FormatException"/>.
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
    /// A standard numeric format specifier that selects the textual representation. <c>D</c>, <c>N</c>, and
    /// <c>X</c> accept an optional precision (for example <c>D5</c>, <c>N0</c>, or <c>X8</c>); <c>G</c> accepts
    /// no precision and rejects a precision specifier with <see cref="FormatException"/>.
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
        return TryFormatMagnitude(this, negative: false, destination, out charsWritten, format, provider);
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
    /// A standard numeric format specifier that selects the textual representation. <c>D</c>, <c>N</c>, and
    /// <c>X</c> accept an optional precision (for example <c>D5</c>, <c>N0</c>, or <c>X8</c>); <c>G</c> accepts
    /// no precision and rejects a precision specifier with <see cref="FormatException"/>.
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
        return TryFormatMagnitude(this, negative: false, utf8Destination, out bytesWritten, format, provider);
    }

    internal static bool TryFormatMagnitude(UInt512 value, bool negative, Span<char> destination,
        out int charsWritten, ReadOnlySpan<char> format, IFormatProvider? provider)
    {
        Span<ulong> limbs = stackalloc ulong[8];
        WideIntegerAlgorithms.WriteLimbs(value, limbs);
        return NumericFormatHelpers.TryFormatUnsigned(limbs, negative, destination, out charsWritten, format, provider);
    }

    internal static bool TryFormatMagnitude(UInt512 value, bool negative, Span<byte> destination,
        out int bytesWritten, ReadOnlySpan<char> format, IFormatProvider? provider)
    {
        Span<ulong> limbs = stackalloc ulong[8];
        WideIntegerAlgorithms.WriteLimbs(value, limbs);
        return NumericFormatHelpers.TryFormatUnsignedUtf8(limbs, negative, destination, out bytesWritten, format, provider);
    }

    #region Parsing

    /// <summary>Converts the string representation of a number to its <see cref="UInt512"/> equivalent.</summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <returns>The parsed unsigned 512-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="s"/> is <see langword="null"/>.</exception>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> does not represent a valid integer in the default style.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[0, 2^512 - 1]</c>.
    /// </exception>
    public static UInt512 Parse(string s) => Parse(s, NumberStyles.Integer, CultureInfo.CurrentCulture);

    /// <summary>
    /// Converts the string representation of a number to its <see cref="UInt512"/> equivalent using the specified
    /// format provider.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed unsigned 512-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="s"/> is <see langword="null"/>.</exception>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> does not represent a valid integer in the default style.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[0, 2^512 - 1]</c>.
    /// </exception>
    public static UInt512 Parse(string s, IFormatProvider? provider) => Parse(s, NumberStyles.Integer, provider);

    /// <summary>
    /// Converts the string representation of a number in a specified style and culture-specific format to its <see cref="UInt512"/> equivalent.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// Every subset of <see cref="NumberStyles.Any"/> is supported for decimal text, and hexadecimal text accepts
    /// subsets of <see cref="NumberStyles.HexNumber"/>; <see cref="NumberStyles.AllowBinarySpecifier"/> is not
    /// supported and fails parsing instead of throwing <see cref="ArgumentException"/>.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed unsigned 512-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="s"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentException">
    /// <paramref name="style"/> contains undefined <see cref="NumberStyles"/> flags, or combines
    /// <see cref="NumberStyles.AllowHexSpecifier"/> or <see cref="NumberStyles.AllowBinarySpecifier"/> with flags
    /// outside <see cref="NumberStyles.HexNumber"/> or <see cref="NumberStyles.BinaryNumber"/> respectively.
    /// </exception>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> is empty or does not represent a valid value under <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[0, 2^512 - 1]</c>.
    /// </exception>
    public static UInt512 Parse(string s, NumberStyles style, IFormatProvider? provider = null)
    {
        ArgumentNullException.ThrowIfNull(s);
        return ParseCore(s.AsSpan(), style, provider);
    }

    /// <summary>
    /// Converts the span representation of a number in a specified style and culture-specific format to its <see cref="UInt512"/> equivalent.
    /// </summary>
    /// <param name="s">A character span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// Every subset of <see cref="NumberStyles.Any"/> is supported for decimal text, and hexadecimal text accepts
    /// subsets of <see cref="NumberStyles.HexNumber"/>; <see cref="NumberStyles.AllowBinarySpecifier"/> is not
    /// supported and fails parsing instead of throwing <see cref="ArgumentException"/>.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed unsigned 512-bit value represented by <paramref name="s"/>.</returns>
    /// <exception cref="ArgumentException">
    /// <paramref name="style"/> contains undefined <see cref="NumberStyles"/> flags, or combines
    /// <see cref="NumberStyles.AllowHexSpecifier"/> or <see cref="NumberStyles.AllowBinarySpecifier"/> with flags
    /// outside <see cref="NumberStyles.HexNumber"/> or <see cref="NumberStyles.BinaryNumber"/> respectively.
    /// </exception>
    /// <exception cref="FormatException">
    /// <paramref name="s"/> is empty or does not represent a valid value under <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="s"/> represents a value outside the inclusive range <c>[0, 2^512 - 1]</c>.
    /// </exception>
    public static UInt512 Parse(
        ReadOnlySpan<char> s,
        NumberStyles style = NumberStyles.Integer,
        IFormatProvider? provider = null)
    {
        return ParseCore(s, style, provider);
    }

    /// <summary>
    /// Converts the UTF-8 span representation of a number in a specified style and culture-specific format to its <see cref="UInt512"/> equivalent.
    /// </summary>
    /// <param name="utf8Text">A UTF-8 byte span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// Every subset of <see cref="NumberStyles.Any"/> is supported for decimal text, and hexadecimal text accepts
    /// subsets of <see cref="NumberStyles.HexNumber"/>; <see cref="NumberStyles.AllowBinarySpecifier"/> is not
    /// supported and fails parsing instead of throwing <see cref="ArgumentException"/>.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <returns>The parsed unsigned 512-bit value represented by <paramref name="utf8Text"/>.</returns>
    /// <exception cref="ArgumentException">
    /// <paramref name="style"/> contains undefined <see cref="NumberStyles"/> flags, or combines
    /// <see cref="NumberStyles.AllowHexSpecifier"/> or <see cref="NumberStyles.AllowBinarySpecifier"/> with flags
    /// outside <see cref="NumberStyles.HexNumber"/> or <see cref="NumberStyles.BinaryNumber"/> respectively.
    /// </exception>
    /// <exception cref="FormatException">
    /// <paramref name="utf8Text"/> is empty or does not represent a valid value under <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// <paramref name="utf8Text"/> represents a value outside the inclusive range <c>[0, 2^512 - 1]</c>.
    /// </exception>
    public static UInt512 Parse(
        ReadOnlySpan<byte> utf8Text,
        NumberStyles style = NumberStyles.Integer,
        IFormatProvider? provider = null)
    {
        return NumericParseHelpers.ParseUtf8(utf8Text, style, provider, ParseCore);
    }

    /// <summary>
    /// Attempts to convert the string representation of a number to its <see cref="UInt512"/> equivalent.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 512-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse([NotNullWhen(true)] string? s, out UInt512 result)
        => TryParse(s, NumberStyles.Integer, CultureInfo.CurrentCulture, out result);

    /// <summary>
    /// Attempts to convert the string representation of a number in a specified style and culture-specific format to
    /// its <see cref="UInt512"/> equivalent.
    /// </summary>
    /// <param name="s">A string that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// Every subset of <see cref="NumberStyles.Any"/> is supported for decimal text, and hexadecimal text accepts
    /// subsets of <see cref="NumberStyles.HexNumber"/>; <see cref="NumberStyles.AllowBinarySpecifier"/> is not
    /// supported and fails parsing instead of throwing <see cref="ArgumentException"/>.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 512-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentException">
    /// <paramref name="style"/> contains undefined <see cref="NumberStyles"/> flags, or combines
    /// <see cref="NumberStyles.AllowHexSpecifier"/> or <see cref="NumberStyles.AllowBinarySpecifier"/> with flags
    /// outside <see cref="NumberStyles.HexNumber"/> or <see cref="NumberStyles.BinaryNumber"/> respectively.
    /// </exception>
    public static bool TryParse(
        [NotNullWhen(true)] string? s,
        NumberStyles style,
        IFormatProvider? provider,
        out UInt512 result)
    {
        if (s is not null)
            return TryParseCore(s.AsSpan(), style, provider, out result);
        result = default;
        return false;
    }

    /// <summary>
    /// Attempts to convert the span representation of a number to its <see cref="UInt512"/> equivalent.
    /// </summary>
    /// <param name="s">A character span that contains the number to convert.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 512-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(ReadOnlySpan<char> s, out UInt512 result)
        => TryParse(s, NumberStyles.Integer, CultureInfo.CurrentCulture, out result);

    /// <summary>
    /// Attempts to convert the span representation of a number in a specified style and culture-specific format to its
    /// <see cref="UInt512"/> equivalent.
    /// </summary>
    /// <param name="s">A character span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// Every subset of <see cref="NumberStyles.Any"/> is supported for decimal text, and hexadecimal text accepts
    /// subsets of <see cref="NumberStyles.HexNumber"/>; <see cref="NumberStyles.AllowBinarySpecifier"/> is not
    /// supported and fails parsing instead of throwing <see cref="ArgumentException"/>.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 512-bit value represented by
    /// <paramref name="s"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentException">
    /// <paramref name="style"/> contains undefined <see cref="NumberStyles"/> flags, or combines
    /// <see cref="NumberStyles.AllowHexSpecifier"/> or <see cref="NumberStyles.AllowBinarySpecifier"/> with flags
    /// outside <see cref="NumberStyles.HexNumber"/> or <see cref="NumberStyles.BinaryNumber"/> respectively.
    /// </exception>
    public static bool TryParse(
        ReadOnlySpan<char> s,
        NumberStyles style,
        IFormatProvider? provider,
        out UInt512 result) =>
        TryParseCore(s, style, provider, out result);

    /// <summary>
    /// Attempts to convert the UTF-8 span representation of a number to its <see cref="UInt512"/> equivalent.
    /// </summary>
    /// <param name="utf8Text">A UTF-8 byte span that contains the number to convert.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 512-bit value represented by
    /// <paramref name="utf8Text"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryParse(ReadOnlySpan<byte> utf8Text, out UInt512 result)
        => TryParse(utf8Text, NumberStyles.Integer, CultureInfo.CurrentCulture, out result);

    /// <summary>
    /// Attempts to convert the UTF-8 span representation of a number in a specified style and culture-specific format
    /// to its <see cref="UInt512"/> equivalent.
    /// </summary>
    /// <param name="utf8Text">A UTF-8 byte span that contains the number to convert.</param>
    /// <param name="style">
    /// A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
    /// Every subset of <see cref="NumberStyles.Any"/> is supported for decimal text, and hexadecimal text accepts
    /// subsets of <see cref="NumberStyles.HexNumber"/>; <see cref="NumberStyles.AllowBinarySpecifier"/> is not
    /// supported and fails parsing instead of throwing <see cref="ArgumentException"/>.
    /// </param>
    /// <param name="provider">An object that supplies culture-specific parsing information.</param>
    /// <param name="result">
    /// When this method returns <see langword="true"/>, contains the parsed unsigned 512-bit value represented by
    /// <paramref name="utf8Text"/>. When this method returns <see langword="false"/>, contains <see cref="Zero"/>.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if parsing succeeds and the value is within range; otherwise, <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentException">
    /// <paramref name="style"/> contains undefined <see cref="NumberStyles"/> flags, or combines
    /// <see cref="NumberStyles.AllowHexSpecifier"/> or <see cref="NumberStyles.AllowBinarySpecifier"/> with flags
    /// outside <see cref="NumberStyles.HexNumber"/> or <see cref="NumberStyles.BinaryNumber"/> respectively.
    /// </exception>
    public static bool TryParse(
        ReadOnlySpan<byte> utf8Text,
        NumberStyles style,
        IFormatProvider? provider,
        out UInt512 result) =>
        NumericParseHelpers.TryParseUtf8(utf8Text, style, provider, TryParseCore, out result);

    #endregion

    #region Operators (unchecked)

    /// <summary>Determines whether two values are equal.</summary>
    /// <param name="left">The value on the left side of the equality comparison.</param>
    /// <param name="right">The value on the right side of the equality comparison.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator ==(UInt512 left, UInt512 right) => left.Equals(right);

    /// <summary>Determines whether two values are not equal.</summary>
    /// <param name="left">The value on the left side of the inequality comparison.</param>
    /// <param name="right">The value on the right side of the inequality comparison.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator !=(UInt512 left, UInt512 right) => !left.Equals(right);

    /// <summary>Determines whether one value is less than another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator <(UInt512 left, UInt512 right) => left.CompareTo(right) < 0;

    /// <summary>Determines whether one value is less than or equal to another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator <=(UInt512 left, UInt512 right) => left.CompareTo(right) <= 0;

    /// <summary>Determines whether one value is greater than another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator >(UInt512 left, UInt512 right) => left.CompareTo(right) > 0;

    /// <summary>Determines whether one value is greater than or equal to another value.</summary>
    /// <param name="left">The value whose order relative to <paramref name="right"/> is tested.</param>
    /// <param name="right">The value used as the ordering reference for <paramref name="left"/>.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator >=(UInt512 left, UInt512 right) => left.CompareTo(right) >= 0;

    /// <summary>Adds two unsigned 512-bit values.</summary>
    /// <param name="left">The augend (the value to which <paramref name="right"/> is added).</param>
    /// <param name="right">The addend (the value added to <paramref name="left"/>).</param>
    public static UInt512 operator +(UInt512 left, UInt512 right)
    {
        UInt256 lo = left._lower + right._lower;
        return new(left._upper + right._upper + (lo < left._lower ? UInt256.One : UInt256.Zero), lo);
    }

    /// <summary>Subtracts one unsigned 512-bit value from another.</summary>
    /// <param name="left">The minuend (the value from which <paramref name="right"/> is subtracted).</param>
    /// <param name="right">The subtrahend (the value subtracted from <paramref name="left"/>).</param>
    public static UInt512 operator -(UInt512 left, UInt512 right) =>
        new(
            left._upper - right._upper - (left._lower < right._lower ? UInt256.One : UInt256.Zero),
            left._lower - right._lower);

    /// <summary>Computes the bitwise AND of two values.</summary>
    /// <param name="left">The first bit pattern operand.</param>
    /// <param name="right">The second bit pattern operand.</param>
    public static UInt512 operator &(UInt512 left, UInt512 right) =>
        new(left._upper & right._upper, left._lower & right._lower);

    /// <summary>Computes the bitwise OR of two values.</summary>
    /// <param name="left">The first bit pattern operand.</param>
    /// <param name="right">The second bit pattern operand.</param>
    public static UInt512 operator |(UInt512 left, UInt512 right) =>
        new(left._upper | right._upper, left._lower | right._lower);

    /// <summary>Computes the bitwise exclusive OR of two values.</summary>
    /// <param name="left">The first bit pattern operand.</param>
    /// <param name="right">The second bit pattern operand.</param>
    public static UInt512 operator ^(UInt512 left, UInt512 right) =>
        new(left._upper ^ right._upper, left._lower ^ right._lower);

    /// <summary>Computes the bitwise complement of a value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt512 operator ~(UInt512 value) => new(~value._upper, ~value._lower);

    /// <summary>Returns the value unchanged.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt512 operator +(UInt512 value) => value;

    /// <summary>Increments a value by one.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt512 operator ++(UInt512 value) => value + One;

    /// <summary>Decrements a value by one.</summary>
    /// <param name="value">The operand value for the operation.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt512 operator --(UInt512 value) => value - One;

    /// <summary>Shifts a value left by the specified number of bits.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="shiftAmount">The number of bit positions to shift.</param>
    public static UInt512 operator <<(UInt512 value, int shiftAmount)
    {
        shiftAmount &= 0x1FF;
        return shiftAmount switch
        {
            0 => value,
            < 256 => new(
                (value._upper << shiftAmount) | (value._lower >> (256 - shiftAmount)),
                value._lower << shiftAmount),
            256 => new(value._lower, 0),
            _ => new(value._lower << (shiftAmount - 256), 0)
        };
    }

    /// <summary>Shifts a value right by the specified number of bits.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="shiftAmount">The number of bit positions to shift.</param>
    public static UInt512 operator >>(UInt512 value, int shiftAmount)
    {
        shiftAmount &= 0x1FF;
        return shiftAmount switch
        {
            0 => value,
            < 256 => new(
                value._upper >> shiftAmount,
                (value._lower >> shiftAmount) | (value._upper << (256 - shiftAmount))),
            256 => new(0, value._upper),
            _ => new(0, value._upper >> (shiftAmount - 256))
        };
    }

    /// <summary>Shifts a value right by the specified number of bits with zero-fill semantics.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="shiftAmount">The number of bit positions to shift.</param>
    /// <remarks>
    /// For unsigned values, logical and arithmetic right shifts are equivalent; this operator forwards to
    /// <see cref="operator >>(UInt512, int)"/>.
    /// </remarks>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt512 operator >>>(UInt512 value, int shiftAmount) => value >> shiftAmount;

    /// <summary>Multiplies two unsigned 512-bit values.</summary>
    /// <param name="left">The multiplicand.</param>
    /// <param name="right">The multiplier.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt512 operator *(UInt512 left, UInt512 right) => Multiply(left, right, out _);

    /// <summary>Divides one unsigned 512-bit value by another.</summary>
    /// <param name="left">The dividend.</param>
    /// <param name="right">The divisor.</param>
    public static UInt512 operator /(UInt512 left, UInt512 right)
    {
        Divide(left, right, out UInt512 quotient, out _);
        return quotient;
    }

    /// <summary>Computes the remainder after dividing one unsigned 512-bit value by another.</summary>
    /// <param name="left">The dividend from which the remainder is computed.</param>
    /// <param name="right">The divisor used to compute the remainder.</param>
    public static UInt512 operator %(UInt512 left, UInt512 right)
    {
        Divide(left, right, out _, out UInt512 remainder);
        return remainder;
    }

    #endregion

    #region checked operators

    /// <summary>Adds two values and throws when the result is outside the <see cref="UInt512"/> range.</summary>
    /// <param name="left">The augend (the value to which <paramref name="right"/> is added).</param>
    /// <param name="right">The addend (the value added to <paramref name="left"/>).</param>
    /// <exception cref="OverflowException">The result cannot be represented as <see cref="UInt512"/>.</exception>
    public static UInt512 operator checked +(UInt512 left, UInt512 right)
    {
        UInt256 lo = left._lower + right._lower;
        UInt256 carry = lo < left._lower ? UInt256.One : UInt256.Zero;
        UInt256 hi = checked(left._upper + right._upper + carry);
        return new(hi, lo);
    }

    /// <summary>Subtracts one value from another and throws when the result would be negative.</summary>
    /// <param name="left">The minuend (the value from which <paramref name="right"/> is subtracted).</param>
    /// <param name="right">The subtrahend (the value subtracted from <paramref name="left"/>).</param>
    /// <exception cref="OverflowException">
    /// The subtraction result is outside the <see cref="UInt512"/> range.
    /// </exception>
    public static UInt512 operator checked -(UInt512 left, UInt512 right)
    {
        UInt256 lo = left._lower - right._lower;
        UInt256 borrow = lo > left._lower ? UInt256.One : UInt256.Zero;
        UInt256 hi = checked(left._upper - right._upper - borrow);
        return new(hi, lo);
    }

    /// <summary>Multiplies two values and throws when the result does not fit in 512 bits.</summary>
    /// <param name="left">The multiplicand.</param>
    /// <param name="right">The multiplier.</param>
    /// <exception cref="OverflowException">The product is outside the <see cref="UInt512"/> range.</exception>
    public static UInt512 operator checked *(UInt512 left, UInt512 right)
    {
        UInt512 product = Multiply(left, right, out bool overflow);
        return overflow
            ? throw new OverflowException()
            : product;
    }

    /// <summary>Divides one value by another in a checked context.</summary>
    /// <param name="left">The dividend.</param>
    /// <param name="right">The divisor.</param>
    /// <exception cref="DivideByZeroException"><paramref name="right"/> is zero.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt512 operator checked /(UInt512 left, UInt512 right) => left / right;

    /// <summary>Increments a value by one and throws when the input is <see cref="MaxValue"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is <see cref="MaxValue"/>.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt512 operator checked ++(UInt512 value) => checked(value + One);

    /// <summary>Decrements a value by one and throws when the input is <see cref="Zero"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is <see cref="Zero"/>.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt512 operator checked --(UInt512 value) => checked(value - One);

    #endregion

    #region Bit helpers

    /// <summary>Counts the number of leading zero bits in the specified value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int LeadingZeroCount(UInt512 value) =>
        value._upper == 0
            ? 256 + BitHelpers.LeadingZeroCount(value._lower)
            : BitHelpers.LeadingZeroCount(value._upper);

    /// <summary>Counts the number of trailing zero bits in the specified value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int TrailingZeroCount(UInt512 value) =>
        value._lower == 0
            ? 256 + BitHelpers.TrailingZeroCount(value._upper)
            : BitHelpers.TrailingZeroCount(value._lower);

    /// <summary>Gets the number of set bits in the specified value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int PopCount(UInt512 value)
        => BitHelpers.PopCount(value._lower) + BitHelpers.PopCount(value._upper);

    /// <summary>Rotates all bits in the value left by a specified amount.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="rotateAmount">The number of bit positions to rotate.</param>
    public static UInt512 RotateLeft(UInt512 value, int rotateAmount)
        => (value << rotateAmount) | (value >> (Bits - rotateAmount));

    /// <summary>Rotates all bits in the value right by a specified amount.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <param name="rotateAmount">The number of bit positions to rotate.</param>
    public static UInt512 RotateRight(UInt512 value, int rotateAmount)
        => (value >> rotateAmount) | (value << (Bits - rotateAmount));

    /// <summary>Computes the integer base-2 logarithm of a value.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static int Log2(UInt512 value) =>
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
    /// Formats an unsigned 512-bit value using a supported standard numeric specifier and optional precision.
    /// </summary>
    /// <param name="value">The value to format.</param>
    /// <param name="format">
    /// The requested format. Supported standard specifiers are <c>G</c>/<c>g</c>, <c>D</c>/<c>d</c>,
    /// <c>N</c>/<c>n</c>, and <c>X</c>/<c>x</c>. <c>D</c>, <c>N</c>, and <c>X</c> may carry a non-negative
    /// decimal precision; <c>G</c>/<c>g</c> accepts no precision component.
    /// A <see langword="null"/> or empty value defaults to decimal formatting.
    /// </param>
    /// <param name="provider">
    /// The format provider used for culture-specific decimal digit formatting.
    /// </param>
    /// <returns>The formatted text representation of <paramref name="value"/>.</returns>
    /// <exception cref="FormatException">
    /// Thrown when <paramref name="format"/> is not a supported standard numeric format or has invalid precision.
    /// </exception>
    private static string FormatValue(UInt512 value, string? format, IFormatProvider? provider)
    {
        if (string.IsNullOrEmpty(format))
            return FormatDecimal(value, provider);

        char spec = format[0];
        return spec switch
        {
            'G' or 'g' or 'D' or 'd' when format.Length == 1 => FormatDecimal(value, provider),
            'G' or 'g' => throw new FormatException(),
            'D' or 'd' or 'N' or 'n' => value.ToBigInteger().ToString(format, provider),
            'X' or 'x' => NumericFormatHelpers.ApplyHexPrecision(FormatHex(value, spec == 'x'), format),
            _ => throw new FormatException()
        };
    }

    /// <summary>
    /// Formats an unsigned 512-bit value as base-10 digits.
    /// </summary>
    /// <param name="value">The value to format.</param>
    /// <param name="provider">
    /// The format provider used for culture-specific decimal digit formatting.
    /// </param>
    /// <returns>A decimal string that round-trips through <see cref="Parse(string)"/>.</returns>
    private static string FormatDecimal(UInt512 value, IFormatProvider? provider)
    {
        Span<char> buffer = stackalloc char[155];
        _ = TryFormatMagnitude(value, negative: false, buffer, out int written, default, provider);
        return new string(buffer[..written]);
    }

    /// <summary>
    /// Formats an unsigned 512-bit value as hexadecimal without leading zero digits.
    /// </summary>
    /// <param name="value">The value to format.</param>
    /// <param name="lower">
    /// <see langword="true"/> to emit lowercase digits <c>a-f</c>; <see langword="false"/> to emit uppercase
    /// digits <c>A-F</c>.
    /// </param>
    /// <returns>The hexadecimal representation of <paramref name="value"/>.</returns>
    private static string FormatHex(UInt512 value, bool lower)
    {
        Span<byte> bytes = stackalloc byte[Bytes];
        WriteUInt256LittleEndian(value._lower, bytes[..32]);
        WriteUInt256LittleEndian(value._upper, bytes.Slice(32, 32));
        char a = lower ? 'a' : 'A';
        Span<char> chars = stackalloc char[128];
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
    /// Parses text into an unsigned 512-bit value and throws when parsing fails.
    /// </summary>
    /// <param name="text">The input text to parse.</param>
    /// <param name="style">The accepted style flags.</param>
    /// <param name="provider">
    /// The format provider that supplies culture-specific sign tokens used while parsing decimal input.
    /// </param>
    /// <returns>The parsed value.</returns>
    /// <exception cref="ArgumentException">
    /// <paramref name="style"/> is not a valid <see cref="NumberStyles"/> combination.
    /// </exception>
    /// <exception cref="FormatException">
    /// Thrown when <paramref name="text"/> is syntactically invalid for the provided <paramref name="style"/>.
    /// </exception>
    /// <exception cref="OverflowException">
    /// Thrown when <paramref name="text"/> represents a numeric value larger than <see cref="MaxValue"/>.
    /// </exception>
    private static UInt512 ParseCore(ReadOnlySpan<char> text, NumberStyles style, IFormatProvider? provider) =>
        TryParseCore(text, style, provider, out UInt512 value, out bool overflow)
            ? value
            : throw (overflow
                ? new OverflowException()
                : new FormatException());

    /// <summary>
    /// Attempts to parse text into an unsigned 512-bit value.
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
    private static bool TryParseCore(ReadOnlySpan<char> text, NumberStyles style, IFormatProvider? provider, out UInt512 value)
        => TryParseCore(text, style, provider, out value, out _);

    /// <summary>
    /// Attempts to parse text into an unsigned 512-bit value and reports overflow separately from syntax failures.
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
    /// <exception cref="ArgumentException">
    /// <paramref name="style"/> is not a valid <see cref="NumberStyles"/> combination.
    /// </exception>
    private static bool TryParseCore(ReadOnlySpan<char> text, NumberStyles style, IFormatProvider? provider, out UInt512 value, out bool overflow)
    {
        NumericParseHelpers.ValidateStyle(style);
        value = Zero;
        overflow = false;
        if ((style & NumberStyles.AllowHexSpecifier) != 0)
            return TryParseHex(text, style, out value, out overflow);

        if ((style & NumberStyles.AllowBinarySpecifier) != 0 ||
            !BigInteger.TryParse(text, style, provider, out BigInteger parsed))
            return false;

        if (parsed.Sign < 0 || parsed > s_bigMaxValue)
        {
            overflow = true;
            return false;
        }

        value = (UInt512)parsed;
        return true;
    }

    /// <summary>
    /// Attempts to parse a hexadecimal representation into an unsigned 512-bit value.
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
    /// When this method returns <see langword="false"/>, indicates whether the input exceeded 128 hexadecimal digits.
    /// </param>
    /// <returns><see langword="true"/> when parsing succeeds; otherwise, <see langword="false"/>.</returns>
    private static bool TryParseHex(ReadOnlySpan<char> text, NumberStyles style, out UInt512 value, out bool overflow)
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

        if (text.Length > 128)
        {
            overflow = true;
            return false;
        }

        UInt512 result = Zero;
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
            result = (result << 4) | new UInt512(0, (ulong)d);
        }

        value = result;
        return true;
    }

    /// <summary>
    /// Multiplies two unsigned 512-bit values and reports whether upper discarded bits were produced.
    /// </summary>
    /// <param name="left">The left multiplicand.</param>
    /// <param name="right">The right multiplicand.</param>
    /// <param name="overflow">
    /// Set to <see langword="true"/> when the full 1024-bit product does not fit in 512 bits.
    /// </param>
    /// <returns>The low 512 bits of the product.</returns>
    private static UInt512 Multiply(UInt512 left, UInt512 right, out bool overflow)
    {
        Span<ulong> a = [
            (ulong)left._lower,
            (ulong)(left._lower >> 64),
            (ulong)(left._lower >> 128),
            (ulong)(left._lower >> 192),
            (ulong)left._upper,
            (ulong)(left._upper >> 64),
            (ulong)(left._upper >> 128),
            (ulong)(left._upper >> 192)
        ];
        Span<ulong> b = [
            (ulong)right._lower,
            (ulong)(right._lower >> 64),
            (ulong)(right._lower >> 128),
            (ulong)(right._lower >> 192),
            (ulong)right._upper,
            (ulong)(right._upper >> 64),
            (ulong)(right._upper >> 128),
            (ulong)(right._upper >> 192)
        ];
        Span<ulong> r = stackalloc ulong[16];

        for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
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

        overflow = false;
        for (int i = 8; i < 16; i++)
            overflow |= r[i] != 0;

        UInt256 lower =
            ((UInt256)r[0]) |
            ((UInt256)r[1] << 64) |
            ((UInt256)r[2] << 128) |
            ((UInt256)r[3] << 192);
        UInt256 upper =
            ((UInt256)r[4]) |
            ((UInt256)r[5] << 64) |
            ((UInt256)r[6] << 128) |
            ((UInt256)r[7] << 192);
        return new(upper, lower);
    }

    /// <summary>
    /// Divides one unsigned 512-bit value by another using normalized limb long division.
    /// </summary>
    /// <param name="dividend">The value to divide.</param>
    /// <param name="divisor">The value that divides <paramref name="dividend"/>.</param>
    /// <param name="quotient">Receives the integer quotient.</param>
    /// <param name="remainder">Receives the non-negative remainder.</param>
    /// <exception cref="DivideByZeroException">
    /// Thrown before any quotient or remainder calculation when <paramref name="divisor"/> is zero.
    /// </exception>
    private static void Divide(UInt512 dividend, UInt512 divisor, out UInt512 quotient, out UInt512 remainder)
    {
        Span<ulong> dividendLimbs = stackalloc ulong[8]
        {
            (ulong)dividend._lower.Lower, (ulong)(dividend._lower.Lower >> 64),
            (ulong)dividend._lower.Upper, (ulong)(dividend._lower.Upper >> 64),
            (ulong)dividend._upper.Lower, (ulong)(dividend._upper.Lower >> 64),
            (ulong)dividend._upper.Upper, (ulong)(dividend._upper.Upper >> 64)
        };
        Span<ulong> divisorLimbs = stackalloc ulong[8]
        {
            (ulong)divisor._lower.Lower, (ulong)(divisor._lower.Lower >> 64),
            (ulong)divisor._lower.Upper, (ulong)(divisor._lower.Upper >> 64),
            (ulong)divisor._upper.Lower, (ulong)(divisor._upper.Lower >> 64),
            (ulong)divisor._upper.Upper, (ulong)(divisor._upper.Upper >> 64)
        };
        Span<ulong> quotientLimbs = stackalloc ulong[8];
        Span<ulong> remainderLimbs = stackalloc ulong[8];
        WideIntegerAlgorithms.DivRem(dividendLimbs, divisorLimbs, quotientLimbs, remainderLimbs);
        quotient = WideIntegerAlgorithms.CreateUInt512(quotientLimbs);
        remainder = WideIntegerAlgorithms.CreateUInt512(remainderLimbs);
    }

    #endregion

    #region BigInteger interop

    /// <summary>
    /// Converts this value into an equivalent non-negative <see cref="BigInteger"/>.
    /// </summary>
    /// <returns>A <see cref="BigInteger"/> with the same unsigned 512-bit magnitude.</returns>
    private BigInteger ToBigInteger()
    {
        Span<byte> bytes = stackalloc byte[Bytes];
        bytes.Clear();
        WriteUInt256LittleEndian(_lower, bytes[..32]);
        WriteUInt256LittleEndian(_upper, bytes.Slice(32, 32));
        return new(bytes, isUnsigned: true, isBigEndian: false);
    }

    /// <summary>Converts a <see cref="UInt512"/> value to a <see cref="BigInteger"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator BigInteger(UInt512 value) => value.ToBigInteger();

    /// <summary>
    /// Converts a <see cref="BigInteger"/> to <see cref="UInt512"/> using unchecked wraparound semantics modulo 2^512.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator UInt512(BigInteger value)
    {
        // wraps (mod 2^512) for unchecked operations
        Span<byte> bytes = stackalloc byte[Bytes];
        bytes.Clear();

        // Signed two's-complement bytes let negative inputs wrap correctly when truncated to 512 bits.
        byte[] src = value.ToByteArray(isUnsigned: false, isBigEndian: false);
        int copy = Math.Min(src.Length, Bytes);
        src.AsSpan(0, copy).CopyTo(bytes);

        // Negative values shorter than 64 bytes require sign-extension so truncation still represents mod 2^512.
        if (value.Sign < 0 && copy < Bytes)
            bytes[copy..].Fill(0xFF);

        return ReadFullLittleEndian(bytes);
    }

    /// <summary>
    /// Converts a <see cref="BigInteger"/> to <see cref="UInt512"/> and throws when the source value is negative or
    /// outside the representable range.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">
    /// <paramref name="value"/> is negative or larger than <see cref="MaxValue"/>.
    /// </exception>
    public static explicit operator checked UInt512(BigInteger value)
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
    public static explicit operator double(UInt512 value) => (double)value.ToBigInteger();

    /// <summary>Converts the value to single-precision floating point.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator float(UInt512 value) => (float)value.ToBigInteger();

    /// <summary>Converts the value to decimal floating point.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is outside the <see cref="decimal"/> range.</exception>
    public static explicit operator decimal(UInt512 value) => (decimal)value.ToBigInteger();

    /// <summary>Converts a double-precision value by truncating and clamping to the unsigned range.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt512(double value) =>
        (UInt512)NumericConversionHelpers.FromFloatingPoint(value, BigInteger.Zero, s_bigMaxValue, isChecked: false);

    /// <summary>Converts a double-precision value and throws when it is outside the unsigned range.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is non-finite, negative, or too large.</exception>
    public static explicit operator checked UInt512(double value) =>
        (UInt512)NumericConversionHelpers.FromFloatingPoint(value, BigInteger.Zero, s_bigMaxValue, isChecked: true);

    /// <summary>Converts a single-precision value by truncating and clamping to the unsigned range.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt512(float value) => (UInt512)(double)value;

    /// <summary>Converts a single-precision value and throws when it is outside the unsigned range.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is non-finite, negative, or too large.</exception>
    public static explicit operator checked UInt512(float value) => checked((UInt512)(double)value);

    /// <summary>Converts a decimal value by truncating its fractional component.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator UInt512(decimal value) =>
        value < 0 ? throw new OverflowException() : (UInt512)(BigInteger)value;

    #endregion

    #region Conversions (built-in integral types)

    /// <summary>Converts an 8-bit unsigned value to <see cref="UInt512"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt512(byte value) => new(0, value);

    /// <summary>Converts a 16-bit unsigned value to <see cref="UInt512"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt512(ushort value) => new(0, value);

    /// <summary>Converts a 32-bit unsigned value to <see cref="UInt512"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt512(uint value) => new(0, value);

    /// <summary>Converts a 64-bit unsigned value to <see cref="UInt512"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt512(ulong value) => new(0, value);

    /// <summary>Converts a 128-bit unsigned value to <see cref="UInt512"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt512(UInt128 value) => new(0, (UInt256)value);

    /// <summary>Converts a 256-bit unsigned value to <see cref="UInt512"/>.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static implicit operator UInt512(UInt256 value) => new(0, value);

    /// <summary>Converts an 8-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt512(sbyte value) => (UInt512)(BigInteger)value;

    /// <summary>Converts an 8-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt512(sbyte value) => checked((UInt512)(BigInteger)value);

    /// <summary>Converts a 16-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt512(short value) => (UInt512)(BigInteger)value;

    /// <summary>Converts a 16-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt512(short value) => checked((UInt512)(BigInteger)value);

    /// <summary>Converts a 32-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt512(int value) => (UInt512)(BigInteger)value;

    /// <summary>Converts a 32-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt512(int value) => checked((UInt512)(BigInteger)value);

    /// <summary>Converts a 64-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt512(long value) => (UInt512)(BigInteger)value;

    /// <summary>Converts a 64-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt512(long value) => checked((UInt512)(BigInteger)value);

    /// <summary>Converts a 128-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt512(Int128 value) => (UInt512)(BigInteger)value;

    /// <summary>Converts a 128-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt512(Int128 value) => checked((UInt512)(BigInteger)value);

    /// <summary>Converts a 256-bit signed value with unchecked fixed-width semantics.</summary>
    /// <param name="value">The value to convert.</param>
    public static explicit operator UInt512(Int256 value) => (UInt512)(BigInteger)value;

    /// <summary>Converts a 256-bit signed value with range checking.</summary>
    /// <param name="value">The value to convert.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt512(Int256 value) => checked((UInt512)(BigInteger)value);

    /// <summary>Converts a signed 512-bit value to <see cref="UInt512"/> by reinterpreting its bit pattern.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator UInt512(Int512 value) => new(value.Upper, value.Lower);

    /// <summary>Converts a signed 512-bit value to <see cref="UInt512"/> in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException"><paramref name="value"/> is negative.</exception>
    public static explicit operator checked UInt512(Int512 value) =>
        value.IsNegative
            ? throw new OverflowException()
            : new(value.Upper, value.Lower);

    /// <summary>Converts a <see cref="UInt512"/> value to an 8-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator byte(UInt512 value) => (byte)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to an 8-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="byte"/>.</exception>
    public static explicit operator checked byte(UInt512 value) =>
        checked((byte)(UInt256)value);

    /// <summary>Converts a <see cref="UInt512"/> value to a 16-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator ushort(UInt512 value) => (ushort)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to a 16-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="ushort"/>.</exception>
    public static explicit operator checked ushort(UInt512 value) =>
        checked((ushort)(UInt256)value);

    /// <summary>Converts a <see cref="UInt512"/> value to a 32-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator uint(UInt512 value) => (uint)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to a 32-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="uint"/>.</exception>
    public static explicit operator checked uint(UInt512 value) =>
        checked((uint)(UInt256)value);

    /// <summary>Converts a <see cref="UInt512"/> value to a 64-bit unsigned integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator ulong(UInt512 value) => (ulong)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to a 64-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="ulong"/>.</exception>
    public static explicit operator checked ulong(UInt512 value) =>
        checked((ulong)(UInt256)value);

    /// <summary>
    /// Converts a <see cref="UInt512"/> value to a 256-bit unsigned integer by returning the low 256 bits.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator UInt256(UInt512 value) => value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to a 256-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="UInt256"/>.</exception>
    public static explicit operator checked UInt256(UInt512 value) =>
        value._upper == 0
            ? value._lower
            : throw new OverflowException();

    /// <summary>Converts a <see cref="UInt512"/> value to a 128-bit unsigned integer by returning the low bits.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator UInt128(UInt512 value) => (UInt128)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to a 128-bit unsigned integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="UInt128"/>.</exception>
    public static explicit operator checked UInt128(UInt512 value) =>
        value._upper == 0 ? checked((UInt128)value._lower) : throw new OverflowException();

    /// <summary>Converts a <see cref="UInt512"/> value to an 8-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator sbyte(UInt512 value) => (sbyte)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to an 8-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="sbyte"/>.</exception>
    public static explicit operator checked sbyte(UInt512 value) => checked((sbyte)(ulong)value);

    /// <summary>Converts a <see cref="UInt512"/> value to a 16-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator short(UInt512 value) => (short)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to a 16-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="short"/>.</exception>
    public static explicit operator checked short(UInt512 value) => checked((short)(ulong)value);

    /// <summary>Converts a <see cref="UInt512"/> value to a 32-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator int(UInt512 value) => (int)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to a 32-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="int"/>.</exception>
    public static explicit operator checked int(UInt512 value) => checked((int)(ulong)value);

    /// <summary>Converts a <see cref="UInt512"/> value to a 64-bit signed integer.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator long(UInt512 value) => (long)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to a 64-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="long"/>.</exception>
    public static explicit operator checked long(UInt512 value) => checked((long)(ulong)value);

    /// <summary>
    /// Converts a <see cref="UInt512"/> value to a 256-bit signed integer by reinterpreting the low 256 bits.
    /// </summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator Int256(UInt512 value) => (Int256)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to a 256-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="Int256"/>.</exception>
    public static explicit operator checked Int256(UInt512 value) => checked((Int256)(UInt256)value);

    /// <summary>Converts a <see cref="UInt512"/> value to a 128-bit signed integer by returning the low bits.</summary>
    /// <param name="value">The operand value for the operation.</param>
    public static explicit operator Int128(UInt512 value) => (Int128)value._lower;

    /// <summary>Converts a <see cref="UInt512"/> value to a 128-bit signed integer in a checked context.</summary>
    /// <param name="value">The operand value for the operation.</param>
    /// <exception cref="OverflowException">The value is outside the range of <see cref="Int128"/>.</exception>
    public static explicit operator checked Int128(UInt512 value) =>
        value._upper == 0 ? checked((Int128)value._lower) : throw new OverflowException();

    #endregion

    #region Private endian helpers

    /// <summary>
    /// Reads a 512-bit unsigned integer from a 64-byte little-endian span.
    /// </summary>
    /// <param name="source">A 64-byte source span whose first byte is the least significant byte.</param>
    /// <returns>The decoded <see cref="UInt512"/> value.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static UInt512 ReadFullLittleEndian(ReadOnlySpan<byte> source) =>
        new(
            ReadUInt256LittleEndian(source.Slice(32, 32)),
            ReadUInt256LittleEndian(source[..32]));

    #endregion
}
