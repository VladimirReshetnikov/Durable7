using System.Globalization;
using System.Numerics;
using System.Text;
using Xunit;

namespace Durable7.Numerics.Tests;

/// <summary>
/// Validates the externally observable contract of <see cref="UInt512"/> across arithmetic, bitwise operations,
/// conversion boundaries, parsing, formatting, and binary serialization.
/// </summary>
/// <remarks>
/// <para>
/// Assertions are derived from <see cref="BigInteger"/> reference calculations normalized to unsigned modulo
/// <c>2^512</c> semantics where appropriate.
/// </para>
/// <para>
/// Randomized test loops use fixed seeds so failures remain deterministic and reproducible.
/// </para>
/// <para>
/// The suite is organized to validate both algebraic identities (for maintainability) and API-level contracts such as
/// parsing style flags, endian readers/writers, and span-based formatting invariants.
/// </para>
/// </remarks>
public sealed class UInt512Tests
{
    /// <summary>
    /// Validates that predefined constants map to expected unsigned numeric boundaries.
    /// </summary>
    [Fact]
    public void StaticFields_AreExpectedConstants()
    {
        Assert.Equal(BigInteger.Zero, (BigInteger)UInt512.Zero);
        Assert.Equal(BigInteger.One, (BigInteger)UInt512.One);
        Assert.Equal(IntegerTestHelpers.UInt512Max, (BigInteger)UInt512.MaxValue);
        Assert.Equal(BigInteger.Zero, (BigInteger)UInt512.MinValue);
    }

    /// <summary>
    /// Verifies public constructors preserve upper/lower halves and unsigned primitive initialization semantics.
    /// </summary>
    [Fact]
    public void Constructors_InitializeExpectedBitPattern()
    {
        var upper = ((UInt256)0x0123_4567_89AB_CDEFul << 64) | 0x0FED_CBA9_8765_4321ul;
        var lower = ((UInt256)0x1122_3344_5566_7788ul << 64) | 0x99AA_BBCC_DDEE_FF00ul;

        var fromHalves = new UInt512(upper, lower);
        var expectedFromHalves = ((BigInteger)upper << 256) | (BigInteger)lower;

        Assert.Equal(expectedFromHalves, (BigInteger)fromHalves);
        Assert.False(fromHalves.IsZero);

        const ulong primitiveValue = 0xDEAD_BEEF_F00D_CAFEuL;
        var fromUlong = new UInt512(primitiveValue);

        Assert.Equal(new BigInteger(primitiveValue), (BigInteger)fromUlong);
        Assert.False(fromUlong.IsZero);
        Assert.True(new UInt512(0ul).IsZero);
    }

    /// <summary>
    /// Verifies unchecked additive operators obey modulo <c>2^512</c> wraparound semantics.
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
            var left = IntegerTestHelpers.RandomUInt512(random);
            var right = IntegerTestHelpers.RandomUInt512(random);

            var expectedAdd = IntegerTestHelpers.NormalizeUnsigned512((BigInteger)left + (BigInteger)right);
            var expectedSub = IntegerTestHelpers.NormalizeUnsigned512((BigInteger)left - (BigInteger)right);

            Assert.Equal(expectedAdd, (BigInteger)(left + right));
            Assert.Equal(expectedSub, (BigInteger)(left - right));
        }

        Assert.Equal(BigInteger.Zero, (BigInteger)(UInt512.MaxValue + UInt512.One));
        Assert.Equal(IntegerTestHelpers.UInt512Max, (BigInteger)(UInt512.Zero - UInt512.One));
    }

    /// <summary>
    /// Verifies checked arithmetic operators throw <see cref="OverflowException"/> when the mathematical result
    /// exceeds the unsigned 512-bit domain.
    /// </summary>
    [Fact]
    public void CheckedArithmetic_ThrowsOnOverflow()
    {
        Assert.Throws<OverflowException>(() => checked(UInt512.MaxValue + UInt512.One));
        Assert.Throws<OverflowException>(() => checked(UInt512.Zero - UInt512.One));
        Assert.Throws<OverflowException>(() => checked(UInt512.MaxValue * UInt512.MaxValue));

        var halfMax = IntegerTestHelpers.UInt512FromBigInteger(IntegerTestHelpers.UInt512Max / 2);
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
            var left = IntegerTestHelpers.RandomUInt512(random);
            var right = IntegerTestHelpers.RandomUInt512(random);
            if (right.IsZero)
            {
                right = UInt512.One;
            }

            var expectedMul = IntegerTestHelpers.NormalizeUnsigned512((BigInteger)left * (BigInteger)right);
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
            var value = IntegerTestHelpers.RandomUInt512(random);
            var other = IntegerTestHelpers.RandomUInt512(random);
            var shift = random.Next(-800, 800);
            var normalizedShift = shift & 0x1FF;

            Assert.Equal((BigInteger)value & (BigInteger)other, (BigInteger)(value & other));
            Assert.Equal((BigInteger)value | (BigInteger)other, (BigInteger)(value | other));
            Assert.Equal((BigInteger)value ^ (BigInteger)other, (BigInteger)(value ^ other));
            Assert.Equal(IntegerTestHelpers.NormalizeUnsigned512(~(BigInteger)value), (BigInteger)~value);

            var expectedLeft = IntegerTestHelpers.NormalizeUnsigned512((BigInteger)value << normalizedShift);
            var expectedRight = (BigInteger)value >> normalizedShift;
            Assert.Equal(expectedLeft, (BigInteger)(value << shift));
            Assert.Equal(expectedRight, (BigInteger)(value >> shift));
            Assert.Equal(expectedRight, (BigInteger)(value >>> shift));

            var expectedRotateLeft = IntegerTestHelpers.NormalizeUnsigned512(
                ((BigInteger)value << normalizedShift) |
                ((BigInteger)value >> (512 - normalizedShift)));
            var expectedRotateRight = IntegerTestHelpers.NormalizeUnsigned512(
                ((BigInteger)value >> normalizedShift) |
                ((BigInteger)value << (512 - normalizedShift)));

            Assert.Equal(expectedRotateLeft, (BigInteger)UInt512.RotateLeft(value, shift));
            Assert.Equal(expectedRotateRight, (BigInteger)UInt512.RotateRight(value, shift));
        }
    }

    /// <summary>
    /// Verifies bit helper methods (<c>LeadingZeroCount</c>, <c>TrailingZeroCount</c>, <c>PopCount</c>, and
    /// <c>Log2</c>).
    /// </summary>
    /// <remarks>
    /// The test covers both zero edge cases and randomized values, including the contract that
    /// <see cref="UInt512.Log2(UInt512)"/> returns zero for <see cref="UInt512.Zero"/>.
    /// </remarks>
    [Fact]
    public void BitHelpers_MatchBitOperationsSemantics()
    {
        Assert.Equal(512, UInt512.LeadingZeroCount(UInt512.Zero));
        Assert.Equal(512, UInt512.TrailingZeroCount(UInt512.Zero));
        Assert.Equal(0, UInt512.PopCount(UInt512.Zero));

        var random = new Random(4521);
        for (var i = 0; i < 250; i++)
        {
            var value = IntegerTestHelpers.RandomUInt512(random);
            var big = (BigInteger)value;

            var expectedLeading = value.IsZero
                ? 512
                : 512 - (int)BigInteger.Log2(big) - 1;

            var expectedTrailing = 0;
            if (value.IsZero)
            {
                expectedTrailing = 512;
            }
            else
            {
                while (((big >> expectedTrailing) & BigInteger.One) == BigInteger.Zero)
                {
                    expectedTrailing++;
                }
            }

            var expectedPop = 0;
            for (var bit = 0; bit < 512; bit++)
            {
                if (((big >> bit) & BigInteger.One) != BigInteger.Zero)
                {
                    expectedPop++;
                }
            }

            Assert.Equal(expectedLeading, UInt512.LeadingZeroCount(value));
            Assert.Equal(expectedTrailing, UInt512.TrailingZeroCount(value));
            Assert.Equal(expectedPop, UInt512.PopCount(value));

            if (!value.IsZero)
            {
                Assert.Equal((int)BigInteger.Log2(big), UInt512.Log2(value));
            }
        }

        Assert.Equal(0, UInt512.Log2(UInt512.Zero));
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
        Span<char> charBuffer = stackalloc char[170];
        Span<byte> byteBuffer = stackalloc byte[170];

        var random = new Random(1010);
        for (var i = 0; i < 120; i++)
        {
            var value = IntegerTestHelpers.RandomUInt512(random);
            var decimalText = value.ToString();

            Assert.Equal(value, UInt512.Parse(decimalText, CultureInfo.InvariantCulture));
            Assert.True(UInt512.TryParse(decimalText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed));
            Assert.Equal(value, parsed);

            var utf8 = Encoding.UTF8.GetBytes(decimalText);
            Assert.Equal(value, UInt512.Parse(utf8, NumberStyles.Integer, CultureInfo.InvariantCulture));
            Assert.True(UInt512.TryParse(utf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed));
            Assert.Equal(value, parsed);

            Span<char> chars = charBuffer[..decimalText.Length];
            Assert.True(value.TryFormat(chars, out var charsWritten, default, CultureInfo.InvariantCulture));
            Assert.Equal(decimalText, chars[..charsWritten].ToString());

            Span<byte> bytes = byteBuffer[..decimalText.Length];
            Assert.True(value.TryFormat(bytes, out var bytesWritten, default, CultureInfo.InvariantCulture));
            Assert.Equal(decimalText, Encoding.UTF8.GetString(bytes[..bytesWritten]));
        }

        Assert.False(UInt512.TryParse("-1", out _));
        Assert.Throws<OverflowException>(() => UInt512.Parse((IntegerTestHelpers.UInt512Max + 1).ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies decimal/general formatting stays aligned with built-in unsigned formatting for the same provider.
    /// </summary>
    [Fact]
    public void Formatting_DecimalAndGeneral_MatchesUInt64ProviderBehavior()
    {
        var provider = (CultureInfo)CultureInfo.InvariantCulture.Clone();
        provider.NumberFormat.NativeDigits = ["٠", "١", "٢", "٣", "٤", "٥", "٦", "٧", "٨", "٩"];

        var value = (UInt512)123;
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
        const string decimalText = "18446744073709551616";
        ReadOnlySpan<char> decimalSpan = decimalText.AsSpan();
        ReadOnlySpan<byte> decimalUtf8 = Encoding.UTF8.GetBytes(decimalText);

        var expected = (UInt512)(BigInteger.One << 64);

        Assert.Equal(expected, UInt512.Parse(decimalText));
        Assert.Equal(expected, UInt512.Parse(decimalText, CultureInfo.InvariantCulture));
        Assert.Equal(expected, UInt512.Parse(decimalSpan, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.True(UInt512.TryParse(decimalText, out var fromString));
        Assert.Equal(expected, fromString);

        Assert.True(UInt512.TryParse(decimalSpan, out var fromSpan));
        Assert.Equal(expected, fromSpan);

        Assert.True(UInt512.TryParse(decimalUtf8, out var fromUtf8));
        Assert.Equal(expected, fromUtf8);
    }

    /// <summary>
    /// Validates style-sensitive parsing edge cases, including hexadecimal input and explicit sign handling.
    /// </summary>
    [Fact]
    public void ParseAndTryParse_RespectNumberStylesForHexAndSigns()
    {
        const string hex = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
        Assert.Equal(UInt512.MaxValue, UInt512.Parse(hex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.True(UInt512.TryParse(hex.AsSpan(), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var parsedHex));
        Assert.Equal(UInt512.MaxValue, parsedHex);

        const string prefixedHex = "0xFF";
        Assert.False(UInt512.TryParse(prefixedHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out _));

        Assert.True(UInt512.TryParse("  +42  ", NumberStyles.Integer, CultureInfo.InvariantCulture, out var signedPositive));
        Assert.Equal((UInt512)42, signedPositive);
        Assert.False(UInt512.TryParse("+", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
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
        const string halfRangeHex = "80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
        var expected = (UInt512)(BigInteger.One << 511);

        Assert.Equal(expected, UInt512.Parse(halfRangeHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.True(UInt512.TryParse(halfRangeHex.AsSpan(), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var parsedSpan));
        Assert.Equal(expected, parsedSpan);

        var utf8 = Encoding.UTF8.GetBytes(halfRangeHex);
        Assert.True(UInt512.TryParse(utf8, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var parsedUtf8));
        Assert.Equal(expected, parsedUtf8);
    }

    /// <summary>
    /// Verifies hexadecimal format specifiers produce trimmed upper/lowercase output and remain compatible with span
    /// formatting APIs.
    /// </summary>
    [Fact]
    public void Formatting_HexSpecifiers_EmitExpectedTextAcrossStringAndSpanPaths()
    {
        var value = IntegerTestHelpers.UInt512FromBigInteger((BigInteger.One << 252) + 0xABCD_EF01_2345_6789);
        var expectedUpper = ((BigInteger)value).ToString("X", CultureInfo.InvariantCulture);
        var expectedLower = expectedUpper.ToLowerInvariant();

        Assert.Equal("0", UInt512.Zero.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal("0", UInt512.Zero.ToString("x", CultureInfo.InvariantCulture));

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
    /// Verifies hexadecimal formatting can emit full-width 512-bit payloads without truncation.
    /// </summary>
    [Fact]
    public void Formatting_HexSpecifiers_FullWidthValues_Emit128Digits()
    {
        var expected = new string('F', 128);
        Assert.Equal(128, expected.Length);

        var actual = UInt512.MaxValue.ToString("X", CultureInfo.InvariantCulture);
        Assert.Equal(expected, actual);

        Span<char> chars = stackalloc char[128];
        Assert.True(UInt512.MaxValue.TryFormat(chars, out var charsWritten, "X", CultureInfo.InvariantCulture));
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
        var value = IntegerTestHelpers.UInt512FromBigInteger((BigInteger.One << 200) + 1234567);

        Assert.Equal(1234567UL, (ulong)(UInt512)1234567UL);
        Assert.Equal(value, (UInt512)(BigInteger)value);
        Assert.Equal((BigInteger)value, (BigInteger)(UInt512)(BigInteger)value);

        Assert.Equal(unchecked((byte)1234567), (byte)value);
        Assert.Equal(unchecked((sbyte)1234567), (sbyte)value);
        Assert.Equal(UInt256.MaxValue, (UInt256)UInt512.MaxValue);
        Assert.Equal(value, checked((UInt512)(BigInteger)value));
        Assert.Equal(UInt512.MaxValue, checked((UInt512)IntegerTestHelpers.UInt512Max));
        Assert.Throws<OverflowException>(() => _ = checked((byte)value));
        Assert.Throws<OverflowException>(() => _ = checked((sbyte)value));
        Assert.Throws<OverflowException>(() => _ = checked((UInt256)UInt512.MaxValue));
        Assert.Throws<OverflowException>(() => _ = checked((int)UInt512.MaxValue));

        var small = (UInt512)42u;
        Assert.Equal((byte)42, (byte)small);
        Assert.Equal((sbyte)42, (sbyte)small);
        Assert.Equal((ushort)42, (ushort)small);
        Assert.Equal(42u, (uint)small);
        Assert.Equal(42UL, (ulong)small);
        Assert.Equal((UInt256)42, (UInt256)small);

        var signedNegative = (Int512)(-1);
        Assert.Equal(UInt512.MaxValue, (UInt512)signedNegative);
        Assert.Throws<OverflowException>(() => _ = checked((UInt512)signedNegative));

        var aboveMax = IntegerTestHelpers.UInt512Max + BigInteger.One;
        Assert.Equal(UInt512.Zero, (UInt512)aboveMax);
        Assert.Throws<OverflowException>(() => _ = checked((UInt512)aboveMax));

        var negativeBigInteger = BigInteger.MinusOne;
        Assert.Equal(UInt512.MaxValue, (UInt512)negativeBigInteger);
        Assert.Throws<OverflowException>(() => _ = checked((UInt512)negativeBigInteger));

        Assert.Equal(UInt512.Zero, checked((UInt512)BigInteger.Zero));
        Assert.Equal(UInt512.MaxValue, checked((UInt512)IntegerTestHelpers.UInt512Max));
    }

    /// <summary>
    /// Verifies checked <see cref="BigInteger"/> to <see cref="UInt512"/> conversion succeeds for representable values.
    /// </summary>
    [Fact]
    public void CheckedBigIntegerConversion_InRangeValues_Succeeds()
    {
        Assert.Equal(UInt512.Zero, checked((UInt512)BigInteger.Zero));
        Assert.Equal(UInt512.One, checked((UInt512)BigInteger.One));

        var mid = (BigInteger.One << 200) + 987654321;
        Assert.Equal((UInt512)mid, checked((UInt512)mid));

        Assert.Equal(UInt512.MaxValue, checked((UInt512)IntegerTestHelpers.UInt512Max));
    }

    /// <summary>
    /// Verifies hexadecimal formatting via standard format strings emits trimmed, case-correct digits.
    /// </summary>
    [Fact]
    public void HexFormatting_UsesTrimmedDigitsAndRequestedCase()
    {
        var value = (UInt512)((BigInteger.One << 200) + 0xABCDEFu);
        var zero = UInt512.Zero;

        var expectedUpper = ((BigInteger)value).ToString("X", CultureInfo.InvariantCulture);
        var expectedLower = expectedUpper.ToLowerInvariant();

        Assert.Equal(expectedUpper, value.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal(expectedLower, value.ToString("x", CultureInfo.InvariantCulture));
        Assert.Equal("0", zero.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal("0", zero.ToString("x", CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies that boundary shift and rotate counts that are multiples of 512 behave as identity operations.
    /// </summary>
    [Fact]
    public void ShiftAndRotate_CountsEquivalentToZero_AreIdentity()
    {
        var value = IntegerTestHelpers.UInt512FromBigInteger((BigInteger.One << 200) + 0x1234);

        foreach (var shift in new[] { 0, 512, 512, -512, -512 })
        {
            Assert.Equal(value, value << shift);
            Assert.Equal(value, value >> shift);
            Assert.Equal(value, UInt512.RotateLeft(value, shift));
            Assert.Equal(value, UInt512.RotateRight(value, shift));
        }
    }

    /// <summary>
    /// Verifies division contracts for zero divisors in both division and remainder operators.
    /// </summary>
    [Fact]
    public void DivisionContracts_ThrowForZeroDivisors()
    {
        Assert.Throws<DivideByZeroException>(() => _ = UInt512.One / UInt512.Zero);
        Assert.Throws<DivideByZeroException>(() => _ = UInt512.One % UInt512.Zero);
        Assert.Throws<DivideByZeroException>(() => _ = checked(UInt512.One / UInt512.Zero));
    }

    /// <summary>
    /// Verifies hexadecimal format specifiers route through the dedicated hex formatter and preserve casing rules.
    /// </summary>
    [Fact]
    public void ToString_HexSpecifiers_ProduceExpectedCanonicalOutput()
    {
        Assert.Equal("0", UInt512.Zero.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal("0", UInt512.Zero.ToString("x", CultureInfo.InvariantCulture));

        var lowNibbleOnly = (UInt512)0x0FUL;
        Assert.Equal("F", lowNibbleOnly.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal("f", lowNibbleOnly.ToString("x", CultureInfo.InvariantCulture));

        var mixed = IntegerTestHelpers.UInt512FromBigInteger(BigInteger.Parse("1234567890ABCDEF1234567890ABCDEF", NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.Equal("1234567890ABCDEF1234567890ABCDEF", mixed.ToString("X", CultureInfo.InvariantCulture));
        Assert.Equal("1234567890abcdef1234567890abcdef", mixed.ToString("x", CultureInfo.InvariantCulture));

        Assert.Equal(mixed.ToString("X", CultureInfo.InvariantCulture), mixed.ToString("X2", CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies fixed-size formatting APIs report failure when destinations are too small.
    /// </summary>
    [Fact]
    public void TryFormat_WithInsufficientDestination_FailsWithoutPartialWrite()
    {
        var value = UInt512.MaxValue;
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
        Assert.True(UInt512.TryParse(padded, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed));
        Assert.Equal((UInt512)42, parsed);

        const string invalidThousands = "1,234";
        Assert.False(UInt512.TryParse(invalidThousands, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.Throws<FormatException>(() => UInt512.Parse(invalidThousands, NumberStyles.Integer, CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies increment/decrement operators around critical boundaries and confirms checked increments throw on
    /// overflow.
    /// </summary>
    [Fact]
    public void IncrementAndDecrement_BoundariesAndCheckedOverflow_AreCorrect()
    {
        var fromZero = UInt512.Zero;
        fromZero++;
        Assert.Equal(UInt512.One, fromZero);

        var fromOne = UInt512.One;
        fromOne--;
        Assert.Equal(UInt512.Zero, fromOne);

        Assert.Equal(UInt512.Zero, UInt512.MaxValue + UInt512.One);
        Assert.Equal(UInt512.MaxValue, UInt512.Zero - UInt512.One);

        Assert.Throws<OverflowException>(() => checked(UInt512.MaxValue + UInt512.One));
        Assert.Throws<OverflowException>(() => checked(UInt512.Zero - UInt512.One));

        var checkedIncrement = checked((UInt512)41);
        checkedIncrement++;
        Assert.Equal((UInt512)42, checkedIncrement);

        var checkedDecrement = checked((UInt512)42);
        checkedDecrement--;
        Assert.Equal((UInt512)41, checkedDecrement);

        Assert.Throws<OverflowException>(() =>
        {
            var value = UInt512.MaxValue;
            checked
            {
                value++;
            }
        });
        Assert.Throws<OverflowException>(() =>
        {
            var value = UInt512.Zero;
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

        Assert.Throws<FormatException>(() => UInt512.Parse("".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Throws<FormatException>(() => UInt512.Parse(ReadOnlySpan<byte>.Empty, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.False(UInt512.TryParse(emptyChars, NumberStyles.Integer, CultureInfo.InvariantCulture, out var charResult));
        Assert.Equal(UInt512.Zero, charResult);

        Assert.False(UInt512.TryParse(emptyUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out var utf8Result));
        Assert.Equal(UInt512.Zero, utf8Result);
    }

    /// <summary>
    /// Verifies parse APIs reject malformed UTF-8 payloads and hexadecimal values outside the 512-bit domain.
    /// </summary>
    [Fact]
    public void Parsing_InvalidUtf8AndOverwideHex_FailsWithContractBehavior()
    {
        byte[] invalidUtf8 = [0x2D, 0x31, 0xC3, 0x28];
        Assert.False(UInt512.TryParse(invalidUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.Throws<FormatException>(() => UInt512.Parse(invalidUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture));

        var overwideHex = new string('F', 129);
        Assert.False(UInt512.TryParse(overwideHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out _));
        Assert.Throws<OverflowException>(() => UInt512.Parse(overwideHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies byte-count and shortest-bit-length transitions at byte and bit boundaries.
    /// </summary>
    [Fact]
    public void ShortestBitLength_AndByteCount_HandleBoundaryTransitions()
    {
        Assert.Equal(0, UInt512.Zero.GetShortestBitLength());
        Assert.Equal(1, UInt512.One.GetShortestBitLength());
        Assert.Equal(8, ((UInt512)255).GetShortestBitLength());
        Assert.Equal(10, ((UInt512)512).GetShortestBitLength());

        Assert.Equal(64, UInt512.Zero.GetByteCount());
        Assert.Equal(64, ((UInt512)255).GetByteCount());
        Assert.Equal(64, ((UInt512)512).GetByteCount());
    }
}
