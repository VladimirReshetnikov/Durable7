using System.Globalization;
using System.Numerics;
using System.Text;
using Xunit;

namespace Tools.Numerics.Tests;

/// <summary>
/// Validates the externally observable contract of <see cref="UInt1024"/> across arithmetic, bitwise operations,
/// conversion boundaries, parsing, formatting, and binary serialization.
/// </summary>
/// <remarks>
/// <para>
/// Assertions are derived from <see cref="BigInteger"/> reference calculations normalized to unsigned modulo
/// <c>2^1024</c> semantics where appropriate.
/// </para>
/// <para>
/// Randomized test loops use fixed seeds so failures remain deterministic and reproducible.
/// </para>
/// <para>
/// The suite is organized to validate both algebraic identities (for maintainability) and API-level contracts such as
/// parsing style flags, endian readers/writers, and span-based formatting invariants.
/// </para>
/// </remarks>
public sealed class UInt1024Tests
{
    /// <summary>
    /// Validates that predefined constants map to expected unsigned numeric boundaries.
    /// </summary>
    [Fact]
    public void StaticFields_AreExpectedConstants()
    {
        Assert.Equal(BigInteger.Zero, (BigInteger)UInt1024.Zero);
        Assert.Equal(BigInteger.One, (BigInteger)UInt1024.One);
        Assert.Equal(IntegerTestHelpers.UInt1024Max, (BigInteger)UInt1024.MaxValue);
        Assert.Equal(BigInteger.Zero, (BigInteger)UInt1024.MinValue);
    }

    /// <summary>
    /// Verifies public constructors preserve upper/lower halves and unsigned primitive initialization semantics.
    /// </summary>
    [Fact]
    public void Constructors_InitializeExpectedBitPattern()
    {
        var upper = ((UInt512)0x0123_4567_89AB_CDEFul << 128) | 0x0FED_CBA9_8765_4321ul;
        var lower = ((UInt512)0x1122_3344_5566_7788ul << 128) | 0x99AA_BBCC_DDEE_FF00ul;

        var fromHalves = new UInt1024(upper, lower);
        var expectedFromHalves = ((BigInteger)upper << 512) | (BigInteger)lower;

        Assert.Equal(expectedFromHalves, (BigInteger)fromHalves);
        Assert.False(fromHalves.IsZero);

        const ulong primitiveValue = 0xDEAD_BEEF_F00D_CAFEuL;
        var fromUlong = new UInt1024(primitiveValue);

        Assert.Equal(new BigInteger(primitiveValue), (BigInteger)fromUlong);
        Assert.False(fromUlong.IsZero);
        Assert.True(new UInt1024(0ul).IsZero);
    }

    /// <summary>
    /// Verifies unchecked additive operators obey modulo <c>2^1024</c> wraparound semantics.
    /// </summary>
    /// <remarks>
    /// Includes explicit wrap assertions for <c>MaxValue + 1</c> and <c>0 - 1</c> to pin down boundary behavior.
    /// </remarks>
    [Fact]
    public void Add_Subtract_Increment_Decrement_WrapAsExpected()
    {
        var random = new Random(12345);
        for (var i = 0; i < 250; i++)
        {
            var left = IntegerTestHelpers.RandomUInt1024(random);
            var right = IntegerTestHelpers.RandomUInt1024(random);

            var expectedAdd = IntegerTestHelpers.NormalizeUnsigned1024((BigInteger)left + (BigInteger)right);
            var expectedSub = IntegerTestHelpers.NormalizeUnsigned1024((BigInteger)left - (BigInteger)right);

            Assert.Equal(expectedAdd, (BigInteger)(left + right));
            Assert.Equal(expectedSub, (BigInteger)(left - right));
        }

        Assert.Equal(BigInteger.Zero, (BigInteger)(UInt1024.MaxValue + UInt1024.One));
        Assert.Equal(IntegerTestHelpers.UInt1024Max, (BigInteger)(UInt1024.Zero - UInt1024.One));
    }

    /// <summary>
    /// Verifies checked arithmetic operators throw <see cref="OverflowException"/> when the mathematical result
    /// exceeds the unsigned 1024-bit domain.
    /// </summary>
    [Fact]
    public void CheckedArithmetic_ThrowsOnOverflow()
    {
        Assert.Throws<OverflowException>(() => checked(UInt1024.MaxValue + UInt1024.One));
        Assert.Throws<OverflowException>(() => checked(UInt1024.Zero - UInt1024.One));
        Assert.Throws<OverflowException>(() => checked(UInt1024.MaxValue * UInt1024.MaxValue));

        var halfMax = IntegerTestHelpers.UInt1024FromBigInteger(IntegerTestHelpers.UInt1024Max / 2);
        var product = checked(halfMax * 2UL);
        Assert.Equal((BigInteger)halfMax * 2, (BigInteger)product);
    }

    /// <summary>
    /// Verifies multiplication, division, and remainder against <see cref="BigInteger"/> reference computations.
    /// </summary>
    /// <remarks>
    /// Division-by-zero is excluded from the random loop because it is covered by dedicated operator contract tests
    /// in the implementation project.
    /// </remarks>
    [Fact]
    public void Multiply_Divide_Modulus_MatchBigInteger()
    {
        var random = new Random(9876);
        for (var i = 0; i < 200; i++)
        {
            var left = IntegerTestHelpers.RandomUInt1024(random);
            var right = IntegerTestHelpers.RandomUInt1024(random);
            if (right.IsZero)
            {
                right = UInt1024.One;
            }

            var expectedMul = IntegerTestHelpers.NormalizeUnsigned1024((BigInteger)left * (BigInteger)right);
            var expectedDiv = (BigInteger)left / (BigInteger)right;
            var expectedMod = (BigInteger)left % (BigInteger)right;

            Assert.Equal(expectedMul, (BigInteger)(left * right));
            Assert.Equal(expectedDiv, (BigInteger)(left / right));
            Assert.Equal(expectedMod, (BigInteger)(left % right));
        }
    }

    /// <summary>
    /// Verifies bitwise operators, logical shifts, and rotation helpers against explicit reference bit operations.
    /// </summary>
    /// <remarks>
    /// Shift counts intentionally span negative and large values to validate count normalization by low 9 bits.
    /// </remarks>
    [Fact]
    public void Bitwise_Shifts_Rotates_MatchReferenceComputation()
    {
        var random = new Random(112233);
        for (var i = 0; i < 200; i++)
        {
            var value = IntegerTestHelpers.RandomUInt1024(random);
            var other = IntegerTestHelpers.RandomUInt1024(random);
            var shift = random.Next(-800, 800);
            var normalizedShift = shift & 0x3FF;

            Assert.Equal((BigInteger)value & (BigInteger)other, (BigInteger)(value & other));
            Assert.Equal((BigInteger)value | (BigInteger)other, (BigInteger)(value | other));
            Assert.Equal((BigInteger)value ^ (BigInteger)other, (BigInteger)(value ^ other));
            Assert.Equal(IntegerTestHelpers.NormalizeUnsigned1024(~(BigInteger)value), (BigInteger)~value);

            var expectedLeft = IntegerTestHelpers.NormalizeUnsigned1024((BigInteger)value << normalizedShift);
            var expectedRight = (BigInteger)value >> normalizedShift;
            Assert.Equal(expectedLeft, (BigInteger)(value << shift));
            Assert.Equal(expectedRight, (BigInteger)(value >> shift));
            Assert.Equal(expectedRight, (BigInteger)(value >>> shift));

            var expectedRotateLeft = IntegerTestHelpers.NormalizeUnsigned1024(
                ((BigInteger)value << normalizedShift) |
                ((BigInteger)value >> (1024 - normalizedShift)));
            var expectedRotateRight = IntegerTestHelpers.NormalizeUnsigned1024(
                ((BigInteger)value >> normalizedShift) |
                ((BigInteger)value << (1024 - normalizedShift)));

            Assert.Equal(expectedRotateLeft, (BigInteger)UInt1024.RotateLeft(value, shift));
            Assert.Equal(expectedRotateRight, (BigInteger)UInt1024.RotateRight(value, shift));
        }
    }

    /// <summary>
    /// Verifies bit helper methods (<c>LeadingZeroCount</c>, <c>TrailingZeroCount</c>, <c>PopCount</c>, and
    /// <c>Log2</c>).
    /// </summary>
    /// <remarks>
    /// The test covers both zero edge cases and randomized values, including the contract that
    /// <see cref="UInt1024.Log2(UInt1024)"/> returns zero for <see cref="UInt1024.Zero"/>.
    /// </remarks>
    [Fact]
    public void BitHelpers_MatchBitOperationsSemantics()
    {
        Assert.Equal(1024, UInt1024.LeadingZeroCount(UInt1024.Zero));
        Assert.Equal(1024, UInt1024.TrailingZeroCount(UInt1024.Zero));
        Assert.Equal(0, UInt1024.PopCount(UInt1024.Zero));

        var random = new Random(4521);
        for (var i = 0; i < 250; i++)
        {
            var value = IntegerTestHelpers.RandomUInt1024(random);
            var big = (BigInteger)value;

            var expectedLeading = value.IsZero
                ? 1024
                : 1024 - (int)BigInteger.Log2(big) - 1;

            var expectedTrailing = 0;
            if (value.IsZero)
            {
                expectedTrailing = 1024;
            }
            else
            {
                while (((big >> expectedTrailing) & BigInteger.One) == BigInteger.Zero)
                {
                    expectedTrailing++;
                }
            }

            var expectedPop = 0;
            for (var bit = 0; bit < 1024; bit++)
            {
                if (((big >> bit) & BigInteger.One) != BigInteger.Zero)
                {
                    expectedPop++;
                }
            }

            Assert.Equal(expectedLeading, UInt1024.LeadingZeroCount(value));
            Assert.Equal(expectedTrailing, UInt1024.TrailingZeroCount(value));
            Assert.Equal(expectedPop, UInt1024.PopCount(value));

            if (!value.IsZero)
            {
                Assert.Equal((int)BigInteger.Log2(big), UInt1024.Log2(value));
            }
        }

        Assert.Equal(0, UInt1024.Log2(UInt1024.Zero));
    }

    /// <summary>
    /// Validates parse/try-parse/format round-trips across UTF-16 and UTF-8 overloads and confirms overflow handling.
    /// </summary>
    /// <remarks>
    /// Random values are formatted in invariant decimal and then parsed through every public text entry point to
    /// ensure representational consistency.
    /// </remarks>
    [Fact]
    public void Parse_TryParse_AndFormatting_RoundTripAcrossRepresentations()
    {
        Span<char> charBuffer = stackalloc char[320];
        Span<byte> byteBuffer = stackalloc byte[320];

        var random = new Random(1010);
        for (var i = 0; i < 120; i++)
        {
            var value = IntegerTestHelpers.RandomUInt1024(random);
            var decimalText = value.ToString();

            Assert.Equal(value, UInt1024.Parse(decimalText, CultureInfo.InvariantCulture));
            Assert.True(UInt1024.TryParse(decimalText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed));
            Assert.Equal(value, parsed);

            var utf8 = Encoding.UTF8.GetBytes(decimalText);
            Assert.Equal(value, UInt1024.Parse(utf8, NumberStyles.Integer, CultureInfo.InvariantCulture));
            Assert.True(UInt1024.TryParse(utf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed));
            Assert.Equal(value, parsed);

            Span<char> chars = charBuffer[..decimalText.Length];
            Assert.True(value.TryFormat(chars, out var charsWritten, default, CultureInfo.InvariantCulture));
            Assert.Equal(decimalText, chars[..charsWritten].ToString());

            Span<byte> bytes = byteBuffer[..decimalText.Length];
            Assert.True(value.TryFormat(bytes, out var bytesWritten, default, CultureInfo.InvariantCulture));
            Assert.Equal(decimalText, Encoding.UTF8.GetString(bytes[..bytesWritten]));
        }

        Assert.False(UInt1024.TryParse("-1", out _));
        Assert.Throws<OverflowException>(() => UInt1024.Parse((IntegerTestHelpers.UInt1024Max + 1).ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies decimal/general formatting stays aligned with built-in unsigned formatting for the same provider.
    /// </summary>
    [Fact]
    public void Formatting_DecimalAndGeneral_MatchesUInt128ProviderBehavior()
    {
        var provider = (CultureInfo)CultureInfo.InvariantCulture.Clone();
        provider.NumberFormat.NativeDigits = ["٠", "١", "٢", "٣", "٤", "٥", "٦", "٧", "٨", "٩"];

        var value = (UInt1024)123;
        var expected = 123UL.ToString(provider);

        Assert.Equal(expected, value.ToString("D", provider));
        Assert.Equal(expected, value.ToString("G", provider));

        Span<char> chars = stackalloc char[8];
        Assert.True(value.TryFormat(chars, out var charsWritten, "D", provider));
        Assert.Equal(expected, chars[..charsWritten].ToString());

        Span<byte> bytes = stackalloc byte[16];
        Assert.True(value.TryFormat(bytes, out var bytesWritten, "G", provider));
        Assert.Equal(expected, Encoding.UTF8.GetString(bytes[..bytesWritten]));
    }


    /// <summary>
    /// Exercises convenience parse/try-parse overloads that use default style/provider parameters.
    /// </summary>
    [Fact]
    public void ParseAndTryParse_ConvenienceOverloads_AreCovered()
    {
        const string decimalText = "340282366920938463463374607431768211456";
        ReadOnlySpan<char> decimalSpan = decimalText.AsSpan();
        ReadOnlySpan<byte> decimalUtf8 = Encoding.UTF8.GetBytes(decimalText);

        var expected = (UInt1024)(BigInteger.One << 128);

        Assert.Equal(expected, UInt1024.Parse(decimalText));
        Assert.Equal(expected, UInt1024.Parse(decimalText, CultureInfo.InvariantCulture));
        Assert.Equal(expected, UInt1024.Parse(decimalSpan, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.True(UInt1024.TryParse(decimalText, out var fromString));
        Assert.Equal(expected, fromString);

        Assert.True(UInt1024.TryParse(decimalSpan, out var fromSpan));
        Assert.Equal(expected, fromSpan);

        Assert.True(UInt1024.TryParse(decimalUtf8, out var fromUtf8));
        Assert.Equal(expected, fromUtf8);
    }

    /// <summary>
    /// Validates style-sensitive parsing edge cases, including hexadecimal input and explicit sign handling.
    /// </summary>
    [Fact]
    public void ParseAndTryParse_RespectNumberStylesForHexAndSigns()
    {
        const string hex = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
        Assert.Equal(UInt1024.MaxValue, UInt1024.Parse(hex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.True(UInt1024.TryParse(hex.AsSpan(), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var parsedHex));
        Assert.Equal(UInt1024.MaxValue, parsedHex);

        const string prefixedHex = "0xFF";
        Assert.False(UInt1024.TryParse(prefixedHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out _));

        Assert.True(UInt1024.TryParse("  +42  ", NumberStyles.Integer, CultureInfo.InvariantCulture, out var signedPositive));
        Assert.Equal((UInt1024)42, signedPositive);
        Assert.False(UInt1024.TryParse("+", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
    }

    /// <summary>
    /// Verifies that top-bit hexadecimal payloads parse as positive unsigned values rather than signed two's-complement
    /// negatives.
    /// </summary>
    /// <remarks>
    /// <see cref="BigInteger"/> treats hexadecimal text as signed by default, so this test exercises the helper path
    /// that prepends a leading zero nibble for unsigned parsing.
    /// </remarks>
    [Fact]
    public void Parse_HexWithTopBitSet_UsesUnsignedSemantics()
    {
        const string halfRangeHex = "8000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
        var expected = (UInt1024)(BigInteger.One << 1023);

        Assert.Equal(expected, UInt1024.Parse(halfRangeHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.True(UInt1024.TryParse(halfRangeHex.AsSpan(), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var parsedSpan));
        Assert.Equal(expected, parsedSpan);

        var utf8 = Encoding.UTF8.GetBytes(halfRangeHex);
        Assert.True(UInt1024.TryParse(utf8, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var parsedUtf8));
        Assert.Equal(expected, parsedUtf8);
    }

    /// <summary>
    /// Verifies hexadecimal format specifiers produce trimmed upper/lowercase output and remain compatible with span
    /// formatting APIs.
    /// </summary>
    [Fact]
    public void Formatting_HexSpecifiers_EmitExpectedTextAcrossStringAndSpanPaths()
    {
        var value = IntegerTestHelpers.UInt1024FromBigInteger((BigInteger.One << 252) + 0xABCD_EF01_2345_6789);
        var expectedUpper = ((BigInteger)value).ToString("X", CultureInfo.InvariantCulture);
        var expectedLower = expectedUpper.ToLowerInvariant();

        Assert.Equal("0", UInt1024.Zero.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal("0", UInt1024.Zero.ToString("x", CultureInfo.InvariantCulture));

        Assert.Equal(expectedUpper, value.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal(expectedLower, value.ToString("x", CultureInfo.InvariantCulture));

        Span<char> upperChars = stackalloc char[expectedUpper.Length];
        Assert.True(value.TryFormat(upperChars, out var upperCharsWritten, "X", CultureInfo.InvariantCulture));
        Assert.Equal(expectedUpper, upperChars[..upperCharsWritten].ToString());

        Span<char> lowerChars = stackalloc char[expectedLower.Length];
        Assert.True(value.TryFormat(lowerChars, out var lowerCharsWritten, "x", CultureInfo.InvariantCulture));
        Assert.Equal(expectedLower, lowerChars[..lowerCharsWritten].ToString());

        Span<byte> upperBytes = stackalloc byte[expectedUpper.Length];
        Assert.True(value.TryFormat(upperBytes, out var upperBytesWritten, "X", CultureInfo.InvariantCulture));
        Assert.Equal(expectedUpper, Encoding.UTF8.GetString(upperBytes[..upperBytesWritten]));

        Span<byte> lowerBytes = stackalloc byte[expectedLower.Length];
        Assert.True(value.TryFormat(lowerBytes, out var lowerBytesWritten, "x", CultureInfo.InvariantCulture));
        Assert.Equal(expectedLower, Encoding.UTF8.GetString(lowerBytes[..lowerBytesWritten]));
    }


    /// <summary>
    /// Verifies hexadecimal formatting can emit full-width 1024-bit payloads without truncation.
    /// </summary>
    [Fact]
    public void Formatting_HexSpecifiers_FullWidthValues_Emit256Digits()
    {
        var expected = new string('F', 256);
        Assert.Equal(256, expected.Length);

        var actual = UInt1024.MaxValue.ToString("X", CultureInfo.InvariantCulture);
        Assert.Equal(expected, actual);

        Span<char> chars = stackalloc char[256];
        Assert.True(UInt1024.MaxValue.TryFormat(chars, out var charsWritten, "X", CultureInfo.InvariantCulture));
        Assert.Equal(expected, chars[..charsWritten].ToString());
    }

    /// <summary>
    /// Verifies conversion operators for successful in-range conversions and expected overflow failures.
    /// </summary>
    /// <remarks>
    /// Cases include round-tripping through <see cref="BigInteger"/>, narrowing cast overflow checks, and successful
    /// conversion of small values to built-in unsigned integer types.
    /// </remarks>
    [Fact]
    public void Conversions_CheckedAndUncheckedBehaviors_AreCorrect()
    {
        var value = IntegerTestHelpers.UInt1024FromBigInteger((BigInteger.One << 200) + 1234567);

        Assert.Equal(1234567UL, (ulong)(UInt1024)1234567UL);
        Assert.Equal(value, (UInt1024)(BigInteger)value);
        Assert.Equal((BigInteger)value, (BigInteger)(UInt1024)(BigInteger)value);

        Assert.Equal(unchecked((byte)1234567), (byte)value);
        Assert.Equal(unchecked((sbyte)1234567), (sbyte)value);
        Assert.Equal(UInt512.MaxValue, (UInt512)UInt1024.MaxValue);
        Assert.Equal(value, checked((UInt1024)(BigInteger)value));
        Assert.Equal(UInt1024.MaxValue, checked((UInt1024)IntegerTestHelpers.UInt1024Max));
        Assert.Throws<OverflowException>(() => _ = checked((byte)value));
        Assert.Throws<OverflowException>(() => _ = checked((sbyte)value));
        Assert.Throws<OverflowException>(() => _ = checked((UInt512)UInt1024.MaxValue));
        Assert.Throws<OverflowException>(() => _ = checked((int)UInt1024.MaxValue));

        var small = (UInt1024)42u;
        Assert.Equal((byte)42, (byte)small);
        Assert.Equal((sbyte)42, (sbyte)small);
        Assert.Equal((ushort)42, (ushort)small);
        Assert.Equal(42u, (uint)small);
        Assert.Equal(42UL, (ulong)small);
        Assert.Equal((UInt512)42, (UInt512)small);

        var signedNegative = (Int1024)(-1);
        Assert.Equal(UInt1024.MaxValue, (UInt1024)signedNegative);
        Assert.Throws<OverflowException>(() => _ = checked((UInt1024)signedNegative));

        var aboveMax = IntegerTestHelpers.UInt1024Max + BigInteger.One;
        Assert.Equal(UInt1024.Zero, (UInt1024)aboveMax);
        Assert.Throws<OverflowException>(() => _ = checked((UInt1024)aboveMax));

        var negativeBigInteger = BigInteger.MinusOne;
        Assert.Equal(UInt1024.MaxValue, (UInt1024)negativeBigInteger);
        Assert.Throws<OverflowException>(() => _ = checked((UInt1024)negativeBigInteger));

        Assert.Equal(UInt1024.Zero, checked((UInt1024)BigInteger.Zero));
        Assert.Equal(UInt1024.MaxValue, checked((UInt1024)IntegerTestHelpers.UInt1024Max));
    }

    /// <summary>
    /// Verifies checked <see cref="BigInteger"/> to <see cref="UInt1024"/> conversion succeeds for representable values.
    /// </summary>
    [Fact]
    public void CheckedBigIntegerConversion_InRangeValues_Succeeds()
    {
        Assert.Equal(UInt1024.Zero, checked((UInt1024)BigInteger.Zero));
        Assert.Equal(UInt1024.One, checked((UInt1024)BigInteger.One));

        var mid = (BigInteger.One << 200) + 987654321;
        Assert.Equal((UInt1024)mid, checked((UInt1024)mid));

        Assert.Equal(UInt1024.MaxValue, checked((UInt1024)IntegerTestHelpers.UInt1024Max));
    }

    /// <summary>
    /// Verifies hexadecimal formatting via standard format strings emits trimmed, case-correct digits.
    /// </summary>
    [Fact]
    public void HexFormatting_UsesTrimmedDigitsAndRequestedCase()
    {
        var value = (UInt1024)((BigInteger.One << 200) + 0xABCDEFu);
        var zero = UInt1024.Zero;

        var expectedUpper = ((BigInteger)value).ToString("X", CultureInfo.InvariantCulture);
        var expectedLower = expectedUpper.ToLowerInvariant();

        Assert.Equal(expectedUpper, value.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal(expectedLower, value.ToString("x", CultureInfo.InvariantCulture));
        Assert.Equal("0", zero.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal("0", zero.ToString("x", CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies that boundary shift and rotate counts that are multiples of 1024 behave as identity operations.
    /// </summary>
    [Fact]
    public void ShiftAndRotate_CountsEquivalentToZero_AreIdentity()
    {
        var value = IntegerTestHelpers.UInt1024FromBigInteger((BigInteger.One << 200) + 0x1234);

        foreach (var shift in new[] { 0, 1024, 1024, -1024, -1024 })
        {
            Assert.Equal(value, value << shift);
            Assert.Equal(value, value >> shift);
            Assert.Equal(value, UInt1024.RotateLeft(value, shift));
            Assert.Equal(value, UInt1024.RotateRight(value, shift));
        }
    }

    /// <summary>
    /// Verifies division contracts for zero divisors in both division and remainder operators.
    /// </summary>
    [Fact]
    public void DivisionContracts_ThrowForZeroDivisors()
    {
        Assert.Throws<DivideByZeroException>(() => _ = UInt1024.One / UInt1024.Zero);
        Assert.Throws<DivideByZeroException>(() => _ = UInt1024.One % UInt1024.Zero);
        Assert.Throws<DivideByZeroException>(() => _ = checked(UInt1024.One / UInt1024.Zero));
    }

    /// <summary>
    /// Verifies hexadecimal format specifiers route through the dedicated hex formatter and preserve casing rules.
    /// </summary>
    [Fact]
    public void ToString_HexSpecifiers_ProduceExpectedCanonicalOutput()
    {
        Assert.Equal("0", UInt1024.Zero.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal("0", UInt1024.Zero.ToString("x", CultureInfo.InvariantCulture));

        var lowNibbleOnly = (UInt1024)0x0FUL;
        Assert.Equal("F", lowNibbleOnly.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal("f", lowNibbleOnly.ToString("x", CultureInfo.InvariantCulture));

        var mixed = IntegerTestHelpers.UInt1024FromBigInteger(BigInteger.Parse("1234567890ABCDEF1234567890ABCDEF", NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.Equal("1234567890ABCDEF1234567890ABCDEF", mixed.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal("1234567890abcdef1234567890abcdef", mixed.ToString("x", CultureInfo.InvariantCulture));

        Assert.Throws<FormatException>(() => mixed.ToString("X2", CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies fixed-size formatting APIs report failure when destinations are too small.
    /// </summary>
    [Fact]
    public void TryFormat_WithInsufficientDestination_FailsWithoutPartialWrite()
    {
        var value = UInt1024.MaxValue;
        var text = value.ToString();

        Span<char> chars = stackalloc char[text.Length - 1];
        Assert.False(value.TryFormat(chars, out var charsWritten, default, CultureInfo.InvariantCulture));
        Assert.Equal(0, charsWritten);

        Span<byte> bytes = stackalloc byte[text.Length - 1];
        Assert.False(value.TryFormat(bytes, out var bytesWritten, default, CultureInfo.InvariantCulture));
        Assert.Equal(0, bytesWritten);
    }

    /// <summary>
    /// Verifies parsing accepts surrounding whitespace with explicit styles and rejects culture-incompatible
    /// punctuation.
    /// </summary>
    [Fact]
    public void Parse_WithWhitespaceAndCultureSpecificFormatting_FollowsNumberStyles()
    {
        const string padded = "  42	";
        Assert.True(UInt1024.TryParse(padded, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed));
        Assert.Equal((UInt1024)42, parsed);

        const string invalidThousands = "1,234";
        Assert.False(UInt1024.TryParse(invalidThousands, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.Throws<FormatException>(() => UInt1024.Parse(invalidThousands, NumberStyles.Integer, CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies increment/decrement operators around critical boundaries and confirms checked increments throw on
    /// overflow.
    /// </summary>
    [Fact]
    public void IncrementAndDecrement_BoundariesAndCheckedOverflow_AreCorrect()
    {
        var fromZero = UInt1024.Zero;
        fromZero++;
        Assert.Equal(UInt1024.One, fromZero);

        var fromOne = UInt1024.One;
        fromOne--;
        Assert.Equal(UInt1024.Zero, fromOne);

        Assert.Equal(UInt1024.Zero, UInt1024.MaxValue + UInt1024.One);
        Assert.Equal(UInt1024.MaxValue, UInt1024.Zero - UInt1024.One);

        Assert.Throws<OverflowException>(() => checked(UInt1024.MaxValue + UInt1024.One));
        Assert.Throws<OverflowException>(() => checked(UInt1024.Zero - UInt1024.One));

        var checkedIncrement = checked((UInt1024)41);
        checkedIncrement++;
        Assert.Equal((UInt1024)42, checkedIncrement);

        var checkedDecrement = checked((UInt1024)42);
        checkedDecrement--;
        Assert.Equal((UInt1024)41, checkedDecrement);

        Assert.Throws<OverflowException>(() =>
        {
            var value = UInt1024.MaxValue;
            checked
            {
                value++;
            }
        });
        Assert.Throws<OverflowException>(() =>
        {
            var value = UInt1024.Zero;
            checked
            {
                value--;
            }
        });
    }

    /// <summary>
    /// Verifies span-based parse overloads reject empty payloads with <see cref="FormatException"/> while matching
    /// <c>TryParse</c> failure behavior.
    /// </summary>
    [Fact]
    public void Parse_EmptySpans_ThrowFormatException_AndTryParseReturnsFalse()
    {
        ReadOnlySpan<char> emptyChars = "".AsSpan();
        ReadOnlySpan<byte> emptyUtf8 = ReadOnlySpan<byte>.Empty;

        Assert.Throws<FormatException>(() => UInt1024.Parse("".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Throws<FormatException>(() => UInt1024.Parse(ReadOnlySpan<byte>.Empty, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.False(UInt1024.TryParse(emptyChars, NumberStyles.Integer, CultureInfo.InvariantCulture, out var charResult));
        Assert.Equal(UInt1024.Zero, charResult);

        Assert.False(UInt1024.TryParse(emptyUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out var utf8Result));
        Assert.Equal(UInt1024.Zero, utf8Result);
    }

    /// <summary>
    /// Verifies parse APIs reject malformed UTF-8 payloads and hexadecimal values outside the 1024-bit domain.
    /// </summary>
    [Fact]
    public void Parsing_InvalidUtf8AndOverwideHex_FailsWithContractBehavior()
    {
        byte[] invalidUtf8 = [0x2D, 0x31, 0xC3, 0x28];
        Assert.False(UInt1024.TryParse(invalidUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.Throws<FormatException>(() => UInt1024.Parse(invalidUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture));

        var overwideHex = new string('F', 257);
        Assert.False(UInt1024.TryParse(overwideHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out _));
        Assert.Throws<OverflowException>(() => UInt1024.Parse(overwideHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies byte-count and shortest-bit-length transitions at byte and bit boundaries.
    /// </summary>
    [Fact]
    public void ShortestBitLength_AndByteCount_HandleBoundaryTransitions()
    {
        Assert.Equal(0, UInt1024.Zero.GetShortestBitLength());
        Assert.Equal(1, UInt1024.One.GetShortestBitLength());
        Assert.Equal(8, ((UInt1024)255).GetShortestBitLength());
        Assert.Equal(11, ((UInt1024)1024).GetShortestBitLength());

        Assert.Equal(128, UInt1024.Zero.GetByteCount());
        Assert.Equal(128, ((UInt1024)255).GetByteCount());
        Assert.Equal(128, ((UInt1024)1024).GetByteCount());
    }
}
