using System.Globalization;
using System.Numerics;
using System.Reflection;
using Xunit;

namespace Tools.Numerics.Tests;

/// <summary>
/// Verifies that fixed-width integer parsing treats <see cref="NumberStyles"/> as supported flag
/// combinations rather than a handful of exact enum values.
/// </summary>
public sealed class WideIntegerNumberStylesTests
{
    /// <summary>
    /// Gets the fixed-width signed and unsigned integer types covered by the shared style tests.
    /// </summary>
    /// <returns>The test data containing every wide integer type.</returns>
    public static TheoryData<Type> WideIntegerTypes()
    {
        return new TheoryData<Type>
        {
            typeof(UInt256),
            typeof(UInt512),
            typeof(UInt1024),
            typeof(Int256),
            typeof(Int512),
            typeof(Int1024),
        };
    }

    /// <summary>
    /// Verifies decimal and hexadecimal parsers accept the supported style flag combinations.
    /// </summary>
    /// <param name="type">The fixed-width integer type to validate.</param>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void Parse_AcceptsSupportedDecimalAndHexCombinations(Type type)
    {
        Assert.Equal("42", Parse(type, "42", NumberStyles.None).ToString());
        Assert.Equal("42", Parse(type, " +42 ", NumberStyles.Integer).ToString());
        Assert.Equal("42", Parse(type, " 42 ", NumberStyles.AllowLeadingWhite | NumberStyles.AllowTrailingWhite).ToString());
        Assert.Equal("42", Parse(type, " 2A ", NumberStyles.HexNumber).ToString());
    }

    /// <summary>
    /// Verifies unsupported style flags and disallowed syntactic elements are rejected while thousands separators
    /// and an all-zero fractional component follow <see cref="NumberStyles.Number"/> semantics.
    /// </summary>
    /// <param name="type">The fixed-width integer type to validate.</param>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void TryParse_HonorsNumberStylesAndRejectsDisallowedElements(Type type)
    {
        Assert.False(TryParse(type, "+42", NumberStyles.None, out _));
        Assert.False(TryParse(type, " +42 ", NumberStyles.AllowLeadingWhite | NumberStyles.AllowTrailingWhite, out _));
        Assert.False(TryParse(type, " 2A ", NumberStyles.AllowHexSpecifier, out _));
        Assert.False(TryParse(type, "+2A", NumberStyles.HexNumber, out _));
        Assert.True(TryParse(type, "1,000", NumberStyles.Integer | NumberStyles.AllowThousands, out object? thousands));
        Assert.Equal("1000", thousands!.ToString());
        Assert.True(TryParse(type, "1,234.00", NumberStyles.Number, out object? number));
        Assert.Equal("1234", number!.ToString());
        Assert.False(TryParse(type, "1,234.50", NumberStyles.Number, out _));
    }

    /// <summary>
    /// Verifies invalid <see cref="NumberStyles"/> combinations throw <see cref="ArgumentException"/> from
    /// <c>Parse</c> and <c>TryParse</c> alike, matching every BCL numeric type.
    /// </summary>
    /// <param name="type">The fixed-width integer type to validate.</param>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void Parse_And_TryParse_ThrowArgumentException_ForInvalidStyleCombinations(Type type)
    {
        NumberStyles[] invalidStyles =
        {
            NumberStyles.AllowHexSpecifier | NumberStyles.AllowLeadingSign,
            NumberStyles.AllowHexSpecifier | NumberStyles.AllowThousands,
            NumberStyles.AllowHexSpecifier | NumberStyles.AllowBinarySpecifier,
            NumberStyles.AllowBinarySpecifier | NumberStyles.AllowLeadingSign,
            (NumberStyles)0x800,
        };

        foreach (NumberStyles style in invalidStyles)
        {
            AssertThrowsThroughReflection<ArgumentException>(() => Parse(type, "42", style));
            AssertThrowsThroughReflection<ArgumentException>(() => TryParse(type, "42", style, out _));
        }
    }

    /// <summary>
    /// Verifies every UTF-16 and UTF-8 <c>Parse</c>/<c>TryParse</c> overload of the 256-bit types throws
    /// <see cref="ArgumentException"/> for an invalid style combination instead of reporting an input failure.
    /// </summary>
    [Fact]
    public void ParseAndTryParseOverloads_ThrowArgumentException_ForInvalidStyles_UInt256AndInt256()
    {
        const NumberStyles invalid = NumberStyles.AllowHexSpecifier | NumberStyles.AllowLeadingSign;
        CultureInfo culture = CultureInfo.InvariantCulture;

        Assert.Throws<ArgumentException>("style", () => UInt256.Parse("42", invalid, culture));
        Assert.Throws<ArgumentException>("style", () => UInt256.Parse("42".AsSpan(), invalid, culture));
        Assert.Throws<ArgumentException>("style", () => UInt256.Parse("42"u8, invalid, culture));
        Assert.Throws<ArgumentException>("style", () => UInt256.TryParse("42", invalid, culture, out _));
        Assert.Throws<ArgumentException>("style", () => UInt256.TryParse("42".AsSpan(), invalid, culture, out _));
        Assert.Throws<ArgumentException>("style", () => UInt256.TryParse("42"u8, invalid, culture, out _));

        Assert.Throws<ArgumentException>("style", () => Int256.Parse("42", invalid, culture));
        Assert.Throws<ArgumentException>("style", () => Int256.Parse("42".AsSpan(), invalid, culture));
        Assert.Throws<ArgumentException>("style", () => Int256.Parse("42"u8, invalid, culture));
        Assert.Throws<ArgumentException>("style", () => Int256.TryParse("42", invalid, culture, out _));
        Assert.Throws<ArgumentException>("style", () => Int256.TryParse("42".AsSpan(), invalid, culture, out _));
        Assert.Throws<ArgumentException>("style", () => Int256.TryParse("42"u8, invalid, culture, out _));
    }

    /// <summary>
    /// Verifies <see cref="NumberStyles.Any"/> subsets parse currency, exponent, and parenthesized-negative input,
    /// with parenthesized negatives overflowing the unsigned types.
    /// </summary>
    /// <param name="type">The fixed-width integer type to validate.</param>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void Parse_SupportsNumberStylesAny(Type type)
    {
        Assert.Equal("1234", Parse(type, " ¤1,234.00 ", NumberStyles.Any).ToString());
        Assert.Equal("1000", Parse(type, "1E+3", NumberStyles.AllowExponent).ToString());

        if (type.Name.StartsWith("Int", StringComparison.Ordinal))
        {
            Assert.Equal("-42", Parse(type, "(42)", NumberStyles.Any).ToString());
        }
        else
        {
            Assert.False(TryParse(type, "(42)", NumberStyles.Any, out _));
            AssertThrowsThroughReflection<OverflowException>(() => Parse(type, "(42)", NumberStyles.Any));
        }
    }

    /// <summary>
    /// Verifies parenthesized negatives round-trip sign and width at the signed boundaries: the exact minimum
    /// magnitude parses to <see cref="Int256.MinValue"/> and one past it is classified as overflow.
    /// </summary>
    [Fact]
    public void Parse_NumberStylesAny_ParenthesizedNegatives_RoundTripSignAndWidth()
    {
        CultureInfo culture = CultureInfo.InvariantCulture;
        BigInteger minMagnitude = BigInteger.One << 255;

        Assert.Equal(Int256.MinValue, Int256.Parse($"({minMagnitude})", NumberStyles.Any, culture));
        Assert.Equal((Int256)(-12345), Int256.Parse("(12,345.00)", NumberStyles.Any, culture));
        Assert.False(Int256.TryParse($"({minMagnitude + 1})", NumberStyles.Any, culture, out _));
        Assert.Throws<OverflowException>(() => Int256.Parse($"({minMagnitude + 1})", NumberStyles.Any, culture));

        Assert.False(UInt256.TryParse("(1)", NumberStyles.Any, culture, out _));
        Assert.Throws<OverflowException>(() => UInt256.Parse("(1)", NumberStyles.Any, culture));

        Int256 parsed = Int256.Parse("(98765)", NumberStyles.Any, culture);
        Assert.Equal(parsed, Int256.Parse(parsed.ToString(), NumberStyles.Any, culture));
    }

    /// <summary>
    /// Verifies <see cref="NumberStyles.AllowBinarySpecifier"/> is treated as a valid but unsupported style:
    /// parsing fails as an input error rather than an argument error.
    /// </summary>
    /// <param name="type">The fixed-width integer type to validate.</param>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void Parse_And_TryParse_TreatBinarySpecifierAsUnsupported(Type type)
    {
        Assert.False(TryParse(type, "101", NumberStyles.BinaryNumber, out _));
        AssertThrowsThroughReflection<FormatException>(() => Parse(type, "101", NumberStyles.BinaryNumber));
    }

    /// <summary>
    /// Verifies the <c>G</c> format rejects a precision specifier with <see cref="FormatException"/> instead of
    /// silently ignoring it, while the bare specifier keeps producing round-trip decimal digits.
    /// </summary>
    /// <param name="type">The fixed-width integer type to validate.</param>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void Formatting_GeneralFormat_RejectsPrecisionSpecifier(Type type)
    {
        object value = Parse(type, "12345", NumberStyles.Integer);
        MethodInfo? method = type.GetMethod("ToString", new[] { typeof(string), typeof(IFormatProvider) });
        Assert.NotNull(method);

        Assert.Equal("12345", method.Invoke(value, new object?[] { "G", CultureInfo.InvariantCulture }));
        Assert.Equal("12345", method.Invoke(value, new object?[] { "g", CultureInfo.InvariantCulture }));
        AssertThrowsThroughReflection<FormatException>(
            () => method.Invoke(value, new object?[] { "G3", CultureInfo.InvariantCulture }));
        AssertThrowsThroughReflection<FormatException>(
            () => method.Invoke(value, new object?[] { "g2", CultureInfo.InvariantCulture }));
        AssertThrowsThroughReflection<FormatException>(
            () => method.Invoke(value, new object?[] { "G0", CultureInfo.InvariantCulture }));
    }

    /// <summary>
    /// Verifies the UTF-16 and UTF-8 <c>TryFormat</c> overloads propagate the <c>G</c>-precision rejection.
    /// </summary>
    [Fact]
    public void TryFormat_GeneralFormatWithPrecision_ThrowsFormatException()
    {
        Assert.Throws<FormatException>(() =>
        {
            Span<char> buffer = stackalloc char[128];
            ((UInt256)12345).TryFormat(buffer, out _, "G3", CultureInfo.InvariantCulture);
        });
        Assert.Throws<FormatException>(() =>
        {
            Span<byte> buffer = stackalloc byte[128];
            ((Int256)12345).TryFormat(buffer, out _, "G3", CultureInfo.InvariantCulture);
        });
    }

    /// <summary>Verifies precision and numeric-grouping format specifiers across all six wide types.</summary>
    /// <param name="type">The fixed-width integer type to validate.</param>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void Formatting_AcceptsPrecisionAndNumericSpecifiers(Type type)
    {
        object value = Parse(type, "1234", NumberStyles.Integer);
        MethodInfo? method = type.GetMethod("ToString", new[] { typeof(string), typeof(IFormatProvider) });
        Assert.NotNull(method);

        Assert.Equal("000004D2", method.Invoke(value, new object?[] { "X8", CultureInfo.InvariantCulture }));
        Assert.Equal("01234", method.Invoke(value, new object?[] { "D5", CultureInfo.InvariantCulture }));
        Assert.Equal("1,234", method.Invoke(value, new object?[] { "N0", CultureInfo.InvariantCulture }));
    }

    /// <summary>Verifies the tractable generic parsing and min/max interface cluster on every wide type.</summary>
    [Fact]
    public void GenericParsingAndMinMaxInterfaces_AreImplementedAcrossWidths()
    {
        AssertGenericInterfaces<UInt256>();
        AssertGenericInterfaces<UInt512>();
        AssertGenericInterfaces<UInt1024>();
        AssertGenericInterfaces<Int256>();
        AssertGenericInterfaces<Int512>();
        AssertGenericInterfaces<Int1024>();
    }

    /// <summary>
    /// Verifies UTF-8 parsing follows the same supported style combinations as UTF-16 parsing.
    /// </summary>
    [Fact]
    public void Utf8Parse_AcceptsSupportedDecimalAndHexCombinations()
    {
        Assert.Equal((UInt256)42, UInt256.Parse(" 2A "u8, NumberStyles.HexNumber, CultureInfo.InvariantCulture));
        Assert.Equal((UInt512)42, UInt512.Parse(" 2A "u8, NumberStyles.HexNumber, CultureInfo.InvariantCulture));
        Assert.Equal((UInt1024)42, UInt1024.Parse(" 2A "u8, NumberStyles.HexNumber, CultureInfo.InvariantCulture));

        Assert.Equal((Int256)(-42), Int256.Parse(" -42 "u8, NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal((Int512)(-42), Int512.Parse(" -42 "u8, NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal((Int1024)(-42), Int1024.Parse(" -42 "u8, NumberStyles.Integer, CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies UTF-8 parsing rejects unsupported style flags and disallowed syntactic elements.
    /// </summary>
    [Fact]
    public void Utf8TryParse_RejectsDisallowedStyleElements()
    {
        Assert.False(UInt256.TryParse("+42"u8, NumberStyles.None, CultureInfo.InvariantCulture, out _));
        Assert.False(UInt512.TryParse("+42"u8, NumberStyles.None, CultureInfo.InvariantCulture, out _));
        Assert.False(UInt1024.TryParse("+42"u8, NumberStyles.None, CultureInfo.InvariantCulture, out _));

        Assert.False(Int256.TryParse("+2A"u8, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out _));
        Assert.False(Int512.TryParse("+2A"u8, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out _));
        Assert.False(Int1024.TryParse("+2A"u8, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out _));
    }

    /// <summary>
    /// Verifies redundant leading zeros in hexadecimal input do not count against the digit budget,
    /// matching the built-in integer parsers, while significant digits past the budget still overflow.
    /// </summary>
    /// <param name="type">The fixed-width integer type to validate.</param>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void Parse_HexIgnoresRedundantLeadingZeros(Type type)
    {
        int hexDigits = type.Name.Contains("1024") ? 256 : type.Name.Contains("512") ? 128 : 64;

        Assert.Equal("255", Parse(type, new string('0', hexDigits) + "FF", NumberStyles.HexNumber).ToString());
        Assert.Equal("0", Parse(type, new string('0', hexDigits + 8), NumberStyles.HexNumber).ToString());

        // A significant digit beyond the budget still overflows.
        Assert.False(TryParse(type, "1" + new string('0', hexDigits), NumberStyles.HexNumber, out _));

        // Leading zeros do not change the two's-complement reinterpretation of full-width patterns.
        string fullWidth = new string('F', hexDigits);
        string expected = Parse(type, fullWidth, NumberStyles.HexNumber).ToString()!;
        Assert.Equal(expected, Parse(type, "00" + fullWidth, NumberStyles.HexNumber).ToString());
    }

    private static object Parse(Type type, string text, NumberStyles style)
    {
        var method = type.GetMethod(
            "Parse",
            BindingFlags.Public | BindingFlags.Static,
            binder: null,
            types: new[] { typeof(string), typeof(NumberStyles), typeof(IFormatProvider) },
            modifiers: null);
        Assert.NotNull(method);
        return method.Invoke(null, new object?[] { text, style, CultureInfo.InvariantCulture })!;
    }

    private static bool TryParse(Type type, string text, NumberStyles style, out object? value)
    {
        var method = type.GetMethod(
            "TryParse",
            BindingFlags.Public | BindingFlags.Static,
            binder: null,
            types: new[] { typeof(string), typeof(NumberStyles), typeof(IFormatProvider), type.MakeByRefType() },
            modifiers: null);
        Assert.NotNull(method);

        object?[] arguments = { text, style, CultureInfo.InvariantCulture, Activator.CreateInstance(type) };
        var parsed = (bool)method.Invoke(null, arguments)!;
        value = arguments[3];
        return parsed;
    }

    private static void AssertThrowsThroughReflection<TException>(Action action)
        where TException : Exception
    {
        var exception = Assert.Throws<TargetInvocationException>(action);
        Assert.IsType<TException>(exception.InnerException);
    }

    private static void AssertGenericInterfaces<T>()
        where T : IParsable<T>, ISpanParsable<T>, IMinMaxValue<T>
    {
        T fromString = T.Parse("42", CultureInfo.InvariantCulture);
        T fromSpan = T.Parse("42".AsSpan(), CultureInfo.InvariantCulture);
        Assert.Equal(fromString, fromSpan);
        Assert.True(T.TryParse("43", CultureInfo.InvariantCulture, out _));
        Assert.True(T.TryParse("44".AsSpan(), CultureInfo.InvariantCulture, out _));
        Assert.NotEqual(T.MinValue, T.MaxValue);
    }
}
