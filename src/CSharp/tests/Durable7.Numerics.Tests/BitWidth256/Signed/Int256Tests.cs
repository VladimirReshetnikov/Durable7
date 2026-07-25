using System.Globalization;
using System.Numerics;
using System.Text;
using Xunit;

namespace Durable7.Numerics.Tests;

/// <summary>
/// Validates the externally observable contract of <see cref="Int256"/> across arithmetic, bitwise operations,
/// conversion boundaries, parsing, formatting, and binary serialization.
/// </summary>
/// <remarks>
/// <para>
/// The suite treats <see cref="BigInteger"/> as the canonical mathematical model and then applies explicit
/// normalization into the signed 256-bit domain. This keeps expectations precise while avoiding accidental
/// re-implementation of production algorithms.
/// </para>
/// <para>
/// Tests intentionally combine directed edge cases with deterministic pseudo-random coverage. Fixed seeds preserve
/// reproducibility, which is critical when diagnosing subtle failures around overflow, sign propagation, and shift
/// normalization rules.
/// </para>
/// <para>
/// Alongside arithmetic validation, these tests pin down contract details around two's-complement edge cases,
/// style-aware parsing, UTF-8 formatting, and endian conversion APIs.
/// </para>
/// </remarks>
public sealed class Int256Tests
{
    /// <summary>
    /// Validates that predefined constants map to the expected numeric boundaries.
    /// </summary>
    [Fact]
    public void StaticFields_AreExpectedConstants()
    {
        Assert.Equal(BigInteger.Zero, (BigInteger)Int256.Zero);
        Assert.Equal(BigInteger.One, (BigInteger)Int256.One);
        Assert.Equal(IntegerTestHelpers.IntMax, (BigInteger)Int256.MaxValue);
        Assert.Equal(IntegerTestHelpers.IntMin, (BigInteger)Int256.MinValue);
        Assert.False(Int256.Zero.IsNegative);
        Assert.False(Int256.One.IsNegative);
        Assert.True(Int256.MinValue.IsNegative);
        Assert.True(((Int256)(-1)).IsNegative);
    }

    /// <summary>
    /// Validates <see cref="Int256.IsNegative"/> against known boundaries and randomized signed values.
    /// </summary>
    [Fact]
    public void IsNegative_TracksSignedDomainCorrectly()
    {
        Assert.False(Int256.Zero.IsNegative);
        Assert.False(Int256.One.IsNegative);
        Assert.False(Int256.MaxValue.IsNegative);

        Assert.True(Int256.MinValue.IsNegative);
        Assert.True(((Int256)(-1)).IsNegative);
        Assert.True(((Int256)(-123456789)).IsNegative);

        var random = new Random(8675309);
        for (var i = 0; i < 500; i++)
        {
            var value = IntegerTestHelpers.RandomInt256(random);
            Assert.Equal((BigInteger)value < 0, value.IsNegative);
        }
    }

    /// <summary>
    /// Verifies public constructors preserve two's-complement bit patterns and expose consistent sign flags.
    /// </summary>
    [Fact]
    public void Constructors_AndIsNegative_ReportExpectedSemantics()
    {
        var upper = ((UInt128)0xFFFF_FFFF_FFFF_FFFFul << 64) | 0x0123_4567_89AB_CDEFul;
        var lower = ((UInt128)0x0011_2233_4455_6677ul << 64) | 0x8899_AABB_CCDD_EEFFul;

        var fromHalves = new Int256(upper, lower);
        var expected = IntegerTestHelpers.NormalizeSigned(((BigInteger)upper << 128) |  lower);

        Assert.Equal(expected, (BigInteger)fromHalves);
        Assert.True(fromHalves.IsNegative);
        Assert.False(fromHalves.IsZero);

        var fromLongNegative = new Int256(-42);
        Assert.Equal(new BigInteger(-42), (BigInteger)fromLongNegative);
        Assert.True(fromLongNegative.IsNegative);

        var fromLongPositive = new Int256(42);
        Assert.Equal(new BigInteger(42), (BigInteger)fromLongPositive);
        Assert.False(fromLongPositive.IsNegative);

        Assert.False(Int256.Zero.IsNegative);
    }

    /// <summary>
    /// Verifies additive operators and unary negation against signed reference arithmetic, including checked overflow.
    /// </summary>
    /// <remarks>
    /// The loop validates unchecked wrap semantics over many random pairs, while explicit assertions confirm that
    /// checked operators throw <see cref="OverflowException"/> at boundary violations.
    /// </remarks>
    [Fact]
    public void AddSubtractNegateAndCheckedOperators_MatchBigIntegerRules()
    {
        var random = new Random(333);
        for (var i = 0; i < 250; i++)
        {
            var left = IntegerTestHelpers.RandomInt256(random);
            var right = IntegerTestHelpers.RandomInt256(random);

            var expectedAdd = IntegerTestHelpers.NormalizeSigned((BigInteger)left + (BigInteger)right);
            var expectedSub = IntegerTestHelpers.NormalizeSigned((BigInteger)left - (BigInteger)right);
            var expectedNeg = IntegerTestHelpers.NormalizeSigned(-(BigInteger)left);

            Assert.Equal(expectedAdd, (BigInteger)(left + right));
            Assert.Equal(expectedSub, (BigInteger)(left - right));
            Assert.Equal(expectedNeg, (BigInteger)(-left));
        }

        Assert.Throws<OverflowException>(() => checked(Int256.MaxValue + Int256.One));
        Assert.Throws<OverflowException>(() => checked(Int256.MinValue - Int256.One));
        Assert.Throws<OverflowException>(() => checked(-Int256.MinValue));

        var checkedIncrement = checked((Int256)41);
        checkedIncrement++;
        Assert.Equal((Int256)42, checkedIncrement);

        var checkedDecrement = checked((Int256)42);
        checkedDecrement--;
        Assert.Equal((Int256)41, checkedDecrement);

        Assert.Throws<OverflowException>(() =>
        {
            var value = Int256.MaxValue;
            checked
            {
                value++;
            }
        });
        Assert.Throws<OverflowException>(() =>
        {
            var value = Int256.MinValue;
            checked
            {
                value--;
            }
        });

        var sum = checked((Int256)123 + (Int256)456);
        Assert.Equal(new BigInteger(579), (BigInteger)sum);
    }

    /// <summary>
    /// Verifies multiplicative arithmetic, division/modulus semantics, checked multiplication, and sign/absolute-value
    /// helpers.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Division and remainder are compared directly with <see cref="BigInteger"/> for non-zero divisors. Multiplication
    /// expectations are normalized into the signed 256-bit domain to mirror unchecked wrap behavior.
    /// </para>
    /// <para>
    /// The test also documents two contract edges: <see cref="Int256.Abs(Int256)"/> throws for
    /// <see cref="Int256.MinValue"/>, and checked multiplication throws on overflow.
    /// </para>
    /// </remarks>
    [Fact]
    public void MultiplyDivideModulus_CheckedMultiply_AndAbsSign_BehaveCorrectly()
    {
        var random = new Random(4444);
        for (var i = 0; i < 180; i++)
        {
            var left = IntegerTestHelpers.RandomInt256(random);
            var right = IntegerTestHelpers.RandomInt256(random);
            if (right.IsZero)
            {
                right = Int256.One;
            }

            var expectedMul = IntegerTestHelpers.NormalizeSigned((BigInteger)left * (BigInteger)right);
            var expectedDiv = (BigInteger)left / (BigInteger)right;
            var expectedMod = (BigInteger)left % (BigInteger)right;

            Assert.Equal(expectedMul, (BigInteger)(left * right));
            Assert.Equal(expectedDiv, (BigInteger)(left / right));
            Assert.Equal(expectedMod, (BigInteger)(left % right));
        }

        Assert.Throws<OverflowException>(() => checked(Int256.MaxValue * 2));
        Assert.Equal(new BigInteger(0), Int256.Sign(Int256.Zero));
        Assert.Equal(1, Int256.Sign(Int256.One));
        Assert.Equal(-1, Int256.Sign(-1));
        Assert.Equal(new BigInteger(12), (BigInteger)Int256.Abs(-12));
        Assert.Throws<OverflowException>(() => Int256.Abs(Int256.MinValue));
    }

    /// <summary>
    /// Verifies checked and unchecked multiplication semantics around <see cref="Int256.MinValue"/>.
    /// </summary>
    [Fact]
    public void Multiply_MinValueBoundaryContracts_AreEnforcedInCheckedAndUncheckedContexts()
    {
        Assert.Equal(Int256.MinValue, Int256.MinValue * Int256.One);
        Assert.Equal(Int256.MinValue, Int256.MinValue * (Int256)(-1));

        Assert.Equal(Int256.MinValue, checked(Int256.MinValue * Int256.One));
        Assert.Throws<OverflowException>(() => checked(Int256.MinValue * (Int256)(-1)));
        Assert.Throws<OverflowException>(() => checked(Int256.MinValue * (Int256)2));
    }

    /// <summary>
    /// Verifies signed multiplication matches two's-complement modulo semantics even when one operand is
    /// <see cref="Int256.MinValue"/>.
    /// </summary>
    /// <remarks>
    /// <para>
    /// These pairs intentionally include combinations that require preserving the exact bit pattern
    /// <c>0x80..00</c> (<see cref="Int256.MinValue"/>) without routing through <see cref="Int256.Abs(Int256)"/>,
    /// which cannot represent <c>|MinValue|</c> in signed space.
    /// </para>
    /// <para>
    /// The expected values are computed in unbounded arithmetic and normalized back into the signed 256-bit domain.
    /// </para>
    /// </remarks>
    [Fact]
    public void Multiply_MinValueOperandPairs_MatchSignedModuloModel()
    {
        var operandPairs = new (Int256 Left, Int256 Right)[]
        {
            (Int256.MinValue, Int256.One),
            (Int256.One, Int256.MinValue),
            (Int256.MinValue, (Int256)(-1)),
            ((Int256)(-1), Int256.MinValue),
            (Int256.MinValue, (Int256)2),
            ((Int256)2, Int256.MinValue),
            (Int256.MinValue, (Int256)(-2)),
            ((Int256)(-2), Int256.MinValue),
            (Int256.MinValue, (Int256)17),
            ((Int256)17, Int256.MinValue),
            (Int256.MinValue, (Int256)(-17)),
            ((Int256)(-17), Int256.MinValue),
        };

        foreach (var (left, right) in operandPairs)
        {
            var expected = IntegerTestHelpers.NormalizeSigned((BigInteger)left * (BigInteger)right);
            Assert.Equal(expected, (BigInteger)(left * right));
        }
    }

    /// <summary>
    /// Verifies checked multiplication enforces signed overflow boundaries for extreme-sign operand pairs.
    /// </summary>
    /// <remarks>
    /// The success and failure set here mirrors <see cref="Int256"/> checked multiply behavior and guards against
    /// accidental dependence on division or absolute-value helper edge cases.
    /// </remarks>
    [Fact]
    public void CheckedMultiply_ExtremeOperandPairs_FollowSignedOverflowRules()
    {
        Assert.Equal(Int256.MinValue, checked(Int256.MinValue * Int256.One));
        Assert.Equal(Int256.MinValue, checked(Int256.One * Int256.MinValue));
        Assert.Equal(Int256.Zero, checked(Int256.MinValue * Int256.Zero));
        Assert.Equal(Int256.Zero, checked(Int256.Zero * Int256.MinValue));

        Assert.Throws<OverflowException>(() => checked(Int256.MinValue * (Int256)(-1)));
        Assert.Throws<OverflowException>(() => checked((Int256)(-1) * Int256.MinValue));
        Assert.Throws<OverflowException>(() => checked(Int256.MinValue * (Int256)2));
        Assert.Throws<OverflowException>(() => checked((Int256)2 * Int256.MinValue));
        Assert.Throws<OverflowException>(() => checked(Int256.MaxValue * (Int256)2));
        Assert.Throws<OverflowException>(() => checked((Int256)2 * Int256.MaxValue));
    }

    /// <summary>
    /// Verifies checked multiplication against an unbounded reference model over deterministic random inputs.
    /// </summary>
    /// <remarks>
    /// For each operand pair, the test computes the full precision <see cref="BigInteger"/> product and compares it
    /// against the closed <see cref="Int256"/> interval. In-range results must succeed and exactly match checked
    /// arithmetic, while out-of-range results must throw <see cref="OverflowException"/>.
    /// </remarks>
    [Fact]
    public void CheckedMultiply_RandomizedBoundaryCoverage_MatchesSignedRangeModel()
    {
        var random = new Random(90210);

        for (var i = 0; i < 350; i++)
        {
            var left = IntegerTestHelpers.RandomInt256(random);
            var right = IntegerTestHelpers.RandomInt256(random);
            var expected = (BigInteger)left * (BigInteger)right;

            if (expected < IntegerTestHelpers.IntMin || expected > IntegerTestHelpers.IntMax)
            {
                Assert.Throws<OverflowException>(() => checked(left * right));
                continue;
            }

            var checkedProduct = checked(left * right);
            Assert.Equal(expected, (BigInteger)checkedProduct);
        }
    }

    /// <summary>
    /// Verifies bitwise operators, arithmetic shifts, and rotation helpers against a signed two's-complement model.
    /// </summary>
    /// <remarks>
    /// Shift counts intentionally include large positive and negative values to validate normalization to the low
    /// 8 bits (<c>&amp; 0xFF</c>) used by the implementation.
    /// </remarks>
    [Fact]
    public void Bitwise_Shifts_Rotates_MatchSignedReferenceModel()
    {
        var random = new Random(777);
        for (var i = 0; i < 200; i++)
        {
            var value = IntegerTestHelpers.RandomInt256(random);
            var other = IntegerTestHelpers.RandomInt256(random);
            var shift = random.Next(-700, 700);
            var normalizedShift = shift & 0xFF;

            Assert.Equal(IntegerTestHelpers.NormalizeSigned((BigInteger)value & (BigInteger)other), (BigInteger)(value & other));
            Assert.Equal(IntegerTestHelpers.NormalizeSigned((BigInteger)value | (BigInteger)other), (BigInteger)(value | other));
            Assert.Equal(IntegerTestHelpers.NormalizeSigned((BigInteger)value ^ (BigInteger)other), (BigInteger)(value ^ other));
            Assert.Equal(IntegerTestHelpers.NormalizeSigned(~(BigInteger)value), (BigInteger)~value);

            var expectedLeft = IntegerTestHelpers.NormalizeSigned((BigInteger)value << normalizedShift);
            var expectedRight = IntegerTestHelpers.NormalizeSigned((BigInteger)value >> normalizedShift);
            Assert.Equal(expectedLeft, (BigInteger)(value << shift));
            Assert.Equal(expectedRight, (BigInteger)(value >> shift));

            var raw = IntegerTestHelpers.NormalizeUnsigned((BigInteger)value);
            var expectedLogicalRight = IntegerTestHelpers.NormalizeSigned(raw >> normalizedShift);
            Assert.Equal(expectedLogicalRight, (BigInteger)(value >>> shift));
            var expectedRotateLeft = IntegerTestHelpers.NormalizeSigned(
                IntegerTestHelpers.NormalizeUnsigned((raw << normalizedShift) | (raw >> (256 - normalizedShift))));
            var expectedRotateRight = IntegerTestHelpers.NormalizeSigned(
                IntegerTestHelpers.NormalizeUnsigned((raw >> normalizedShift) | (raw << (256 - normalizedShift))));

            Assert.Equal(expectedRotateLeft, (BigInteger)Int256.RotateLeft(value, shift));
            Assert.Equal(expectedRotateRight, (BigInteger)Int256.RotateRight(value, shift));
        }
    }

    /// <summary>
    /// Validates parse/try-parse/format round-trips across UTF-16 and UTF-8 overloads and confirms range failures.
    /// </summary>
    /// <remarks>
    /// Inputs are generated from valid <see cref="Int256"/> instances, ensuring each representation can round-trip
    /// through text and UTF-8 APIs. Explicit out-of-range payloads verify <see cref="OverflowException"/> and
    /// <c>TryParse</c> failure behavior.
    /// </remarks>
    [Fact]
    public void ParseTryParseFormattingAndUtf8RoundTrip()
    {
        Span<char> charBuffer = stackalloc char[80];
        Span<byte> byteBuffer = stackalloc byte[80];

        var random = new Random(2020);
        for (var i = 0; i < 100; i++)
        {
            var value = IntegerTestHelpers.RandomInt256(random);
            var text = value.ToString();

            Assert.Equal(value, Int256.Parse(text, CultureInfo.InvariantCulture));
            Assert.True(Int256.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed));
            Assert.Equal(value, parsed);

            var utf8 = Encoding.UTF8.GetBytes(text);
            Assert.Equal(value, Int256.Parse(utf8, NumberStyles.Integer, CultureInfo.InvariantCulture));
            Assert.True(Int256.TryParse(utf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed));
            Assert.Equal(value, parsed);

            Span<char> chars = charBuffer[..text.Length];
            Assert.True(value.TryFormat(chars, out var charsWritten, default, CultureInfo.InvariantCulture));
            Assert.Equal(text, chars[..charsWritten].ToString());

            Span<byte> bytes = byteBuffer[..text.Length];
            Assert.True(value.TryFormat(bytes, out var bytesWritten, default, CultureInfo.InvariantCulture));
            Assert.Equal(text, Encoding.UTF8.GetString(bytes[..bytesWritten]));
        }

        var outOfRangePositive = (IntegerTestHelpers.IntMax + 1).ToString(CultureInfo.InvariantCulture);
        var outOfRangeNegative = (IntegerTestHelpers.IntMin - 1).ToString(CultureInfo.InvariantCulture);
        Assert.Throws<OverflowException>(() => Int256.Parse(outOfRangePositive, CultureInfo.InvariantCulture));
        Assert.Throws<OverflowException>(() => Int256.Parse(outOfRangeNegative, CultureInfo.InvariantCulture));
        Assert.False(Int256.TryParse(outOfRangePositive, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.False(Int256.TryParse(outOfRangeNegative, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
    }

    /// <summary>
    /// Verifies formatting APIs honor the supplied provider for decimal/general output.
    /// </summary>
    [Fact]
    public void Formatting_DecimalAndGeneral_UseProvidedCultureTokens()
    {
        var provider = (CultureInfo)CultureInfo.InvariantCulture.Clone();
        provider.NumberFormat.NegativeSign = "~";

        var value = (Int256)(-123);
        const string expected = "~123";

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
    /// Verifies division and remainder overflow behavior for the two's-complement minimum value boundary.
    /// </summary>
    [Fact]
    public void DivisionAndRemainder_MinValueCornerCases_AreCorrect()
    {
        Assert.Equal(Int256.MinValue, Int256.MinValue / Int256.One);
        Assert.Equal(Int256.Zero, Int256.MinValue % Int256.One);

        Assert.Throws<OverflowException>(() => checked(Int256.MinValue / (Int256)(-1)));

        Assert.Throws<OverflowException>(() => _ = Int256.MinValue / -1);
        Assert.Throws<OverflowException>(() => _ = Int256.MinValue % -1);
    }

    /// <summary>
    /// Validates explicit conversions from <see cref="UInt256"/> to <see cref="Int256"/> in unchecked and checked
    /// contexts.
    /// </summary>
    [Fact]
    public void ExplicitConversion_FromUInt256_HonorsCheckedSemantics()
    {
        var inRangeUnsigned = (UInt256)Int256.MaxValue;
        var convertedInRange = (Int256)inRangeUnsigned;
        Assert.Equal(Int256.MaxValue, convertedInRange);
        Assert.Equal(Int256.MaxValue, checked((Int256)inRangeUnsigned));

        var wrapsToNegativeOne = UInt256.MaxValue;
        Assert.Equal(-1, (Int256)wrapsToNegativeOne);
        Assert.Throws<OverflowException>(() => checked((Int256)wrapsToNegativeOne));

        var signBitOnly = new UInt256((UInt128)1 << 127, 0);
        Assert.Equal(Int256.MinValue, (Int256)signBitOnly);
        Assert.Throws<OverflowException>(() => checked((Int256)signBitOnly));
    }

    /// <summary>
    /// Validates style-sensitive parsing edges and sign handling for decimal and hexadecimal forms.
    /// </summary>
    [Fact]
    public void ParseAndTryParse_RespectStylesAndRejectMalformedInput()
    {
        const string maxHex = "7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
        Assert.Equal(Int256.MaxValue, Int256.Parse(maxHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));

        const string minHexTwosComplement = "8000000000000000000000000000000000000000000000000000000000000000";
        Assert.Equal(Int256.MinValue, Int256.Parse(minHexTwosComplement, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));

        Assert.True(Int256.TryParse("  -17  ", NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed));
        Assert.Equal(-17, parsed);

        Assert.False(Int256.TryParse("--1", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.False(Int256.TryParse("0x10", NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out _));
    }

    /// <summary>
    /// Exercises convenience parse/try-parse overloads that use default style/provider parameters.
    /// </summary>
    [Fact]
    public void ParseAndTryParse_ConvenienceOverloads_AreCovered()
    {
        const string decimalText = "-18446744073709551616";
        ReadOnlySpan<char> decimalSpan = decimalText.AsSpan();
        ReadOnlySpan<byte> decimalUtf8 = Encoding.UTF8.GetBytes(decimalText);

        var expected = (Int256)(-(BigInteger.One << 64));

        Assert.Equal(expected, Int256.Parse(decimalText));
        Assert.Equal(expected, Int256.Parse(decimalText, CultureInfo.InvariantCulture));
        Assert.Equal(expected, Int256.Parse(decimalSpan, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.True(Int256.TryParse(decimalText, out var fromString));
        Assert.Equal(expected, fromString);

        Assert.True(Int256.TryParse(decimalSpan, out var fromSpan));
        Assert.Equal(expected, fromSpan);

        Assert.True(Int256.TryParse(decimalUtf8, out var fromUtf8));
        Assert.Equal(expected, fromUtf8);
    }

    /// <summary>
    /// Verifies hexadecimal parsing follows signed two's-complement semantics at 256-bit boundaries.
    /// </summary>
    [Fact]
    public void Parse_HexBoundaryValues_MapToExpectedSignedResults()
    {
        const string allOnesHex = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
        Assert.Equal(-1, Int256.Parse(allOnesHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.True(Int256.TryParse(allOnesHex.AsSpan(), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var parsed));
        Assert.Equal(-1, parsed);

        const string minPlusOneHex = "8000000000000000000000000000000000000000000000000000000000000001";
        Assert.Equal(Int256.MinValue + Int256.One, Int256.Parse(minPlusOneHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));

        var utf8 = Encoding.UTF8.GetBytes(minPlusOneHex);
        Assert.True(Int256.TryParse(utf8, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var parsedUtf8));
        Assert.Equal(Int256.MinValue + Int256.One, parsedUtf8);
    }

    /// <summary>
    /// Verifies explicit conversion operators for both successful in-range casts and overflow-protected casts.
    /// </summary>
    /// <remarks>
    /// Cases include narrowing signed casts, signed-to-unsigned rejection for negative values, and conversion from
    /// <see cref="Int256"/> to <see cref="BigInteger"/> through <see cref="UInt256"/> for positive values.
    /// </remarks>
    [Fact]
    public void ConversionOperators_CheckedAndUncheckedBehaviors_AreCorrect()
    {
        Assert.Equal(42L, (long)(Int256)42);
        Assert.Equal(-42, (Int128)(Int256)(-42));
        Assert.Equal(unchecked((byte)-1), (byte)(Int256)(-1));
        Assert.Equal(unchecked((sbyte)-1), (sbyte)(Int256)(-1));
        Assert.Equal(unchecked((UInt128)(-1)), (UInt128)(Int256)(-1));

        var largePositive = (Int256)(new BigInteger(1) << 200);
        Assert.Equal(0L, (long)largePositive);
        Assert.Equal(0, (Int128)largePositive);
        Assert.Throws<OverflowException>(() => _ = checked((long)largePositive));
        Assert.Throws<OverflowException>(() => _ = checked((Int128)largePositive));
        Assert.Throws<OverflowException>(() => _ = checked((sbyte)largePositive));
        Assert.Throws<OverflowException>(() => _ = checked((ulong)(Int256)(-1)));

        var smallNegative = (Int256)(-42);
        Assert.Equal((sbyte)-42, (sbyte)smallNegative);
        Assert.Equal((sbyte)-42, checked((sbyte)smallNegative));

        var negative = (Int256)(-1);
        Assert.Equal(UInt256.MaxValue, (UInt256)negative);
        Assert.Throws<OverflowException>(() => _ = checked((UInt256)negative));

        var positive = (Int256)123;
        Assert.Equal(123, (BigInteger)(UInt256)positive);
        Assert.Equal((UInt256)positive, checked((UInt256)positive));

        Assert.Equal(Int256.MaxValue, checked((Int256)IntegerTestHelpers.IntMax));
        Assert.Equal(Int256.MinValue, checked((Int256)IntegerTestHelpers.IntMin));

        var aboveMax = IntegerTestHelpers.IntMax + BigInteger.One;
        Assert.Equal(Int256.MinValue, (Int256)aboveMax);
        Assert.Throws<OverflowException>(() => _ = checked((Int256)aboveMax));

        var belowMin = IntegerTestHelpers.IntMin - BigInteger.One;
        Assert.Equal(Int256.MaxValue, (Int256)belowMin);
        Assert.Throws<OverflowException>(() => _ = checked((Int256)belowMin));

        Assert.Equal(Int256.MinValue, checked((Int256)IntegerTestHelpers.IntMin));
        Assert.Equal(Int256.MaxValue, checked((Int256)IntegerTestHelpers.IntMax));
    }

    /// <summary>
    /// Verifies checked <see cref="BigInteger"/> to <see cref="Int256"/> conversion succeeds for in-range values.
    /// </summary>
    [Fact]
    public void CheckedBigIntegerConversion_InRangeValues_Succeeds()
    {
        Assert.Equal(Int256.Zero, checked((Int256)BigInteger.Zero));
        Assert.Equal(Int256.One, checked((Int256)BigInteger.One));
        Assert.Equal((Int256)(-1), checked((Int256)BigInteger.MinusOne));

        var midPositive = (BigInteger.One << 200) + 123456789;
        Assert.Equal((Int256)midPositive, checked((Int256)midPositive));

        var midNegative = -((BigInteger.One << 200) + 987654321);
        Assert.Equal((Int256)midNegative, checked((Int256)midNegative));

        Assert.Equal(Int256.MaxValue, checked((Int256)IntegerTestHelpers.IntMax));
        Assert.Equal(Int256.MinValue, checked((Int256)IntegerTestHelpers.IntMin));
    }

    /// <summary>
    /// Verifies that boundary shift and rotate counts that are multiples of 256 behave as identity operations.
    /// </summary>
    /// <remarks>
    /// This test pins down count normalization behavior for exact-width and large-magnitude counts, including
    /// negative values, where the implementation masks counts with <c>0xFF</c>.
    /// </remarks>
    [Fact]
    public void ShiftAndRotate_CountsEquivalentToZero_AreIdentity()
    {
        var value = (Int256)(new BigInteger(1) << 200) - 12345;

        foreach (var shift in new[] { 0, 256, 512, -256, -512 })
        {
            Assert.Equal(value, value << shift);
            Assert.Equal(value, value >> shift);
            Assert.Equal(value, value >>> shift);
            Assert.Equal(value, Int256.RotateLeft(value, shift));
            Assert.Equal(value, Int256.RotateRight(value, shift));
        }
    }

    /// <summary>
    /// Verifies division edge-case behavior for zero divisors and the checked <c>MinValue / -1</c> overflow path.
    /// </summary>
    [Fact]
    public void DivisionContracts_HandleZeroAndCheckedOverflowEdges()
    {
        Assert.Throws<DivideByZeroException>(() => _ = Int256.One / Int256.Zero);
        Assert.Throws<DivideByZeroException>(() => _ = Int256.One % Int256.Zero);
        Assert.Throws<OverflowException>(() => checked(Int256.MinValue / (Int256)(-1)));
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

        Assert.Throws<FormatException>(() => Int256.Parse("".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Throws<FormatException>(() => Int256.Parse(ReadOnlySpan<byte>.Empty, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.False(Int256.TryParse(emptyChars, NumberStyles.Integer, CultureInfo.InvariantCulture, out var charResult));
        Assert.Equal(Int256.Zero, charResult);

        Assert.False(Int256.TryParse(emptyUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out var utf8Result));
        Assert.Equal(Int256.Zero, utf8Result);
    }

    /// <summary>
    /// Verifies fixed-size formatting APIs report failure when destinations are too small.
    /// </summary>
    [Fact]
    public void TryFormat_WithInsufficientDestination_FailsWithoutPartialWrite()
    {
        var value = Int256.MinValue;
        var text = value.ToString();

        Span<char> chars = stackalloc char[text.Length - 1];
        Assert.False(value.TryFormat(chars, out var charsWritten, default, CultureInfo.InvariantCulture));
        Assert.Equal(0, charsWritten);

        Span<byte> bytes = stackalloc byte[text.Length - 1];
        Assert.False(value.TryFormat(bytes, out var bytesWritten, default, CultureInfo.InvariantCulture));
        Assert.Equal(0, bytesWritten);
    }

    /// <summary>
    /// Verifies shortest-bit-length and byte-count calculations for values near sign-boundary transitions.
    /// </summary>
    [Fact]
    public void ShortestBitLength_AndByteCount_HandleSignBoundaryTransitions()
    {
        Assert.Equal(0, Int256.Zero.GetShortestBitLength());
        Assert.Equal(1, ((Int256)(-1)).GetShortestBitLength());
        Assert.Equal(2, ((Int256)(-2)).GetShortestBitLength());
        Assert.Equal(7, ((Int256)127).GetShortestBitLength());
        Assert.Equal(8, ((Int256)128).GetShortestBitLength());

        Assert.Equal(32, Int256.Zero.GetByteCount());
        Assert.Equal(32, ((Int256)(-1)).GetByteCount());
        Assert.Equal(32, ((Int256)127).GetByteCount());
        Assert.Equal(32, ((Int256)128).GetByteCount());

        Assert.Equal(0, Int256.Log2(Int256.Zero));
        Assert.Equal(7, Int256.Log2(128));
        Assert.Throws<ArgumentOutOfRangeException>(() => Int256.Log2(-1));
    }

    /// <summary>
    /// Verifies bit-count helper accuracy for leading zeros, trailing zeros, and population count.
    /// </summary>
    /// <remarks>
    /// Expected values are computed from the canonical unsigned 256-bit pattern corresponding to each signed
    /// sample, ensuring these helpers are validated as bit operations rather than sign-aware arithmetic operations.
    /// </remarks>
    [Fact]
    public void LeadingTrailingAndPopCount_AreBitAccurate()
    {
        Assert.Equal(256, Int256.LeadingZeroCount(Int256.Zero));
        Assert.Equal(256, Int256.TrailingZeroCount(Int256.Zero));
        Assert.Equal(0, Int256.PopCount(Int256.Zero));

        var random = new Random(9001);
        for (var i = 0; i < 200; i++)
        {
            var value = IntegerTestHelpers.RandomInt256(random);
            var raw = IntegerTestHelpers.NormalizeUnsigned((BigInteger)value);

            var expectedLeading = raw.IsZero ? 256 : 256 - (int)BigInteger.Log2(raw) - 1;

            var expectedTrailing = 0;
            if (raw.IsZero)
            {
                expectedTrailing = 256;
            }
            else
            {
                while (((raw >> expectedTrailing) & BigInteger.One) == BigInteger.Zero)
                {
                    expectedTrailing++;
                }
            }

            var expectedPop = 0;
            for (var bit = 0; bit < 256; bit++)
            {
                if (((raw >> bit) & BigInteger.One) != BigInteger.Zero)
                {
                    expectedPop++;
                }
            }

            Assert.Equal(expectedLeading, Int256.LeadingZeroCount(value));
            Assert.Equal(expectedTrailing, Int256.TrailingZeroCount(value));
            Assert.Equal(expectedPop, Int256.PopCount(value));
        }
    }

    /// <summary>
    /// Verifies arithmetic right shift preserves sign for negative values under high and normalized shift counts.
    /// </summary>
    [Fact]
    public void RightShift_OnNegativeValues_PerformsSignExtension()
    {
        var allOnes = (Int256)(-1);
        foreach (var shift in new[] { 1, 63, 127, 128, 255, 256, -1, -257 })
        {
            Assert.Equal(allOnes, allOnes >> shift);
        }

        var min = Int256.MinValue;
        Assert.Equal(-1, min >> 255);
        Assert.Equal(min, min >> 256);
    }

    /// <summary>
    /// Verifies parsing in hexadecimal mode honors two's-complement interpretation at and beyond signed boundaries.
    /// </summary>
    [Fact]
    public void Parse_HexTwoComplementBoundaries_AndOverwideInput_UseExpectedContracts()
    {
        const string negativeOneHex = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
        Assert.Equal(-1, Int256.Parse(negativeOneHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));

        const string minHex = "8000000000000000000000000000000000000000000000000000000000000000";
        Assert.Equal(Int256.MinValue, Int256.Parse(minHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));

        var overwideHex = new string('F', 65);
        Assert.False(Int256.TryParse(overwideHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out _));
        Assert.Throws<OverflowException>(() => Int128.Parse(overwideHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.Throws<OverflowException>(() => Int256.Parse(overwideHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
    }
}
