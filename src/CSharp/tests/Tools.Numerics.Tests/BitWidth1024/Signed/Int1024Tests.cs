using System.Globalization;
using System.Numerics;
using System.Text;
using Xunit;

namespace Tools.Numerics.Tests;

/// <summary>
/// Validates the externally observable contract of <see cref="Int1024"/> across arithmetic, bitwise operations,
/// conversion boundaries, parsing, formatting, and binary serialization.
/// </summary>
/// <remarks>
/// <para>
/// The suite treats <see cref="BigInteger"/> as the canonical mathematical model and then applies explicit
/// normalization into the signed 1024-bit domain. This keeps expectations precise while avoiding accidental
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
public sealed class Int1024Tests
{
    /// <summary>
    /// Validates that predefined constants map to the expected numeric boundaries.
    /// </summary>
    [Fact]
    public void StaticFields_AreExpectedConstants()
    {
        Assert.Equal(BigInteger.Zero, (BigInteger)Int1024.Zero);
        Assert.Equal(BigInteger.One, (BigInteger)Int1024.One);
        Assert.Equal(IntegerTestHelpers.Int1024Max, (BigInteger)Int1024.MaxValue);
        Assert.Equal(IntegerTestHelpers.Int1024Min, (BigInteger)Int1024.MinValue);
        Assert.False(Int1024.Zero.IsNegative);
        Assert.False(Int1024.One.IsNegative);
        Assert.True(Int1024.MinValue.IsNegative);
        Assert.True(((Int1024)(-1)).IsNegative);
    }

    /// <summary>
    /// Validates <see cref="Int1024.IsNegative"/> against known boundaries and randomized signed values.
    /// </summary>
    [Fact]
    public void IsNegative_TracksSignedDomainCorrectly()
    {
        Assert.False(Int1024.Zero.IsNegative);
        Assert.False(Int1024.One.IsNegative);
        Assert.False(Int1024.MaxValue.IsNegative);

        Assert.True(Int1024.MinValue.IsNegative);
        Assert.True(((Int1024)(-1)).IsNegative);
        Assert.True(((Int1024)(-123456789)).IsNegative);

        var random = new Random(8675309);
        for (var i = 0; i < 500; i++)
        {
            var value = IntegerTestHelpers.RandomInt1024(random);
            Assert.Equal((BigInteger)value < 0, value.IsNegative);
        }
    }

    /// <summary>
    /// Verifies public constructors preserve two's-complement bit patterns and expose consistent sign flags.
    /// </summary>
    [Fact]
    public void Constructors_AndIsNegative_ReportExpectedSemantics()
    {
        var upper = ((UInt512)1 << 511) | ((UInt512)0x0123_4567_89AB_CDEFul << 128) | 0x0FED_CBA9_8765_4321ul;
        var lower = ((UInt512)0x0011_2233_4455_6677ul << 128) | 0x8899_AABB_CCDD_EEFFul;

        var fromHalves = new Int1024(upper, lower);
        var expected = IntegerTestHelpers.NormalizeSigned1024(((BigInteger)upper << 512) | (BigInteger)lower);

        Assert.Equal(expected, (BigInteger)fromHalves);
        Assert.True(fromHalves.IsNegative);
        Assert.False(fromHalves.IsZero);

        var fromLongNegative = new Int1024(-42);
        Assert.Equal(new BigInteger(-42), (BigInteger)fromLongNegative);
        Assert.True(fromLongNegative.IsNegative);

        var fromLongPositive = new Int1024(42);
        Assert.Equal(new BigInteger(42), (BigInteger)fromLongPositive);
        Assert.False(fromLongPositive.IsNegative);

        Assert.False(Int1024.Zero.IsNegative);
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
            var left = IntegerTestHelpers.RandomInt1024(random);
            var right = IntegerTestHelpers.RandomInt1024(random);

            var expectedAdd = IntegerTestHelpers.NormalizeSigned1024((BigInteger)left + (BigInteger)right);
            var expectedSub = IntegerTestHelpers.NormalizeSigned1024((BigInteger)left - (BigInteger)right);
            var expectedNeg = IntegerTestHelpers.NormalizeSigned1024(-(BigInteger)left);

            Assert.Equal(expectedAdd, (BigInteger)(left + right));
            Assert.Equal(expectedSub, (BigInteger)(left - right));
            Assert.Equal(expectedNeg, (BigInteger)(-left));
        }

        Assert.Throws<OverflowException>(() => checked(Int1024.MaxValue + Int1024.One));
        Assert.Throws<OverflowException>(() => checked(Int1024.MinValue - Int1024.One));
        Assert.Throws<OverflowException>(() => checked(-Int1024.MinValue));

        var checkedIncrement = checked((Int1024)41);
        checkedIncrement++;
        Assert.Equal((Int1024)42, checkedIncrement);

        var checkedDecrement = checked((Int1024)42);
        checkedDecrement--;
        Assert.Equal((Int1024)41, checkedDecrement);

        Assert.Throws<OverflowException>(() =>
        {
            var value = Int1024.MaxValue;
            checked
            {
                value++;
            }
        });
        Assert.Throws<OverflowException>(() =>
        {
            var value = Int1024.MinValue;
            checked
            {
                value--;
            }
        });

        var sum = checked((Int1024)123 + (Int1024)456);
        Assert.Equal(new BigInteger(579), (BigInteger)sum);
    }

    /// <summary>
    /// Verifies multiplicative arithmetic, division/modulus semantics, checked multiplication, and sign/absolute-value
    /// helpers.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Division and remainder are compared directly with <see cref="BigInteger"/> for non-zero divisors. Multiplication
    /// expectations are normalized into the signed 1024-bit domain to mirror unchecked wrap behavior.
    /// </para>
    /// <para>
    /// The test also documents two contract edges: <see cref="Int1024.Abs(Int1024)"/> throws for
    /// <see cref="Int1024.MinValue"/>, and checked multiplication throws on overflow.
    /// </para>
    /// </remarks>
    [Fact]
    public void MultiplyDivideModulus_CheckedMultiply_AndAbsSign_BehaveCorrectly()
    {
        var random = new Random(4444);
        for (var i = 0; i < 180; i++)
        {
            var left = IntegerTestHelpers.RandomInt1024(random);
            var right = IntegerTestHelpers.RandomInt1024(random);
            if (right.IsZero)
            {
                right = Int1024.One;
            }

            var expectedMul = IntegerTestHelpers.NormalizeSigned1024((BigInteger)left * (BigInteger)right);
            var expectedDiv = (BigInteger)left / (BigInteger)right;
            var expectedMod = (BigInteger)left % (BigInteger)right;

            Assert.Equal(expectedMul, (BigInteger)(left * right));
            Assert.Equal(expectedDiv, (BigInteger)(left / right));
            Assert.Equal(expectedMod, (BigInteger)(left % right));
        }

        Assert.Throws<OverflowException>(() => checked(Int1024.MaxValue * 2));
        Assert.Equal(new BigInteger(0), Int1024.Sign(Int1024.Zero));
        Assert.Equal(1, Int1024.Sign(Int1024.One));
        Assert.Equal(-1, Int1024.Sign(-1));
        Assert.Equal(new BigInteger(12), (BigInteger)Int1024.Abs(-12));
        Assert.Throws<OverflowException>(() => Int1024.Abs(Int1024.MinValue));
    }

    /// <summary>
    /// Verifies checked and unchecked multiplication semantics around <see cref="Int1024.MinValue"/>.
    /// </summary>
    [Fact]
    public void Multiply_MinValueBoundaryContracts_AreEnforcedInCheckedAndUncheckedContexts()
    {
        Assert.Equal(Int1024.MinValue, Int1024.MinValue * Int1024.One);
        Assert.Equal(Int1024.MinValue, Int1024.MinValue * (Int1024)(-1));

        Assert.Equal(Int1024.MinValue, checked(Int1024.MinValue * Int1024.One));
        Assert.Throws<OverflowException>(() => checked(Int1024.MinValue * (Int1024)(-1)));
        Assert.Throws<OverflowException>(() => checked(Int1024.MinValue * (Int1024)2));
    }

    /// <summary>
    /// Verifies signed multiplication matches two's-complement modulo semantics even when one operand is
    /// <see cref="Int1024.MinValue"/>.
    /// </summary>
    /// <remarks>
    /// <para>
    /// These pairs intentionally include combinations that require preserving the exact bit pattern
    /// <c>0x80..00</c> (<see cref="Int1024.MinValue"/>) without routing through <see cref="Int1024.Abs(Int1024)"/>,
    /// which cannot represent <c>|MinValue|</c> in signed space.
    /// </para>
    /// <para>
    /// The expected values are computed in unbounded arithmetic and normalized back into the signed 1024-bit domain.
    /// </para>
    /// </remarks>
    [Fact]
    public void Multiply_MinValueOperandPairs_MatchSignedModuloModel()
    {
        var operandPairs = new (Int1024 Left, Int1024 Right)[]
        {
            (Int1024.MinValue, Int1024.One),
            (Int1024.One, Int1024.MinValue),
            (Int1024.MinValue, (Int1024)(-1)),
            ((Int1024)(-1), Int1024.MinValue),
            (Int1024.MinValue, (Int1024)2),
            ((Int1024)2, Int1024.MinValue),
            (Int1024.MinValue, (Int1024)(-2)),
            ((Int1024)(-2), Int1024.MinValue),
            (Int1024.MinValue, (Int1024)17),
            ((Int1024)17, Int1024.MinValue),
            (Int1024.MinValue, (Int1024)(-17)),
            ((Int1024)(-17), Int1024.MinValue),
        };

        foreach (var (left, right) in operandPairs)
        {
            var expected = IntegerTestHelpers.NormalizeSigned1024((BigInteger)left * (BigInteger)right);
            Assert.Equal(expected, (BigInteger)(left * right));
        }
    }

    /// <summary>
    /// Verifies checked multiplication enforces signed overflow boundaries for extreme-sign operand pairs.
    /// </summary>
    /// <remarks>
    /// The success and failure set here mirrors <see cref="Int1024"/> checked multiply behavior and guards against
    /// accidental dependence on division or absolute-value helper edge cases.
    /// </remarks>
    [Fact]
    public void CheckedMultiply_ExtremeOperandPairs_FollowSignedOverflowRules()
    {
        Assert.Equal(Int1024.MinValue, checked(Int1024.MinValue * Int1024.One));
        Assert.Equal(Int1024.MinValue, checked(Int1024.One * Int1024.MinValue));
        Assert.Equal(Int1024.Zero, checked(Int1024.MinValue * Int1024.Zero));
        Assert.Equal(Int1024.Zero, checked(Int1024.Zero * Int1024.MinValue));

        Assert.Throws<OverflowException>(() => checked(Int1024.MinValue * (Int1024)(-1)));
        Assert.Throws<OverflowException>(() => checked((Int1024)(-1) * Int1024.MinValue));
        Assert.Throws<OverflowException>(() => checked(Int1024.MinValue * (Int1024)2));
        Assert.Throws<OverflowException>(() => checked((Int1024)2 * Int1024.MinValue));
        Assert.Throws<OverflowException>(() => checked(Int1024.MaxValue * (Int1024)2));
        Assert.Throws<OverflowException>(() => checked((Int1024)2 * Int1024.MaxValue));
    }

    /// <summary>
    /// Verifies checked multiplication against an unbounded reference model over deterministic random inputs.
    /// </summary>
    /// <remarks>
    /// For each operand pair, the test computes the full precision <see cref="BigInteger"/> product and compares it
    /// against the closed <see cref="Int1024"/> interval. In-range results must succeed and exactly match checked
    /// arithmetic, while out-of-range results must throw <see cref="OverflowException"/>.
    /// </remarks>
    [Fact]
    public void CheckedMultiply_RandomizedBoundaryCoverage_MatchesSignedRangeModel()
    {
        var random = new Random(90210);

        for (var i = 0; i < 350; i++)
        {
            var left = IntegerTestHelpers.RandomInt1024(random);
            var right = IntegerTestHelpers.RandomInt1024(random);
            var expected = (BigInteger)left * (BigInteger)right;

            if (expected < IntegerTestHelpers.Int1024Min || expected > IntegerTestHelpers.Int1024Max)
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
    /// 9 bits (<c>&amp; 0x3FF</c>) used by the implementation.
    /// </remarks>
    [Fact]
    public void Bitwise_Shifts_Rotates_MatchSignedReferenceModel()
    {
        var random = new Random(777);
        for (var i = 0; i < 200; i++)
        {
            var value = IntegerTestHelpers.RandomInt1024(random);
            var other = IntegerTestHelpers.RandomInt1024(random);
            var shift = random.Next(-700, 700);
            var normalizedShift = shift & 0x3FF;

            Assert.Equal(IntegerTestHelpers.NormalizeSigned1024((BigInteger)value & (BigInteger)other), (BigInteger)(value & other));
            Assert.Equal(IntegerTestHelpers.NormalizeSigned1024((BigInteger)value | (BigInteger)other), (BigInteger)(value | other));
            Assert.Equal(IntegerTestHelpers.NormalizeSigned1024((BigInteger)value ^ (BigInteger)other), (BigInteger)(value ^ other));
            Assert.Equal(IntegerTestHelpers.NormalizeSigned1024(~(BigInteger)value), (BigInteger)~value);

            var expectedLeft = IntegerTestHelpers.NormalizeSigned1024((BigInteger)value << normalizedShift);
            var expectedRight = IntegerTestHelpers.NormalizeSigned1024((BigInteger)value >> normalizedShift);
            Assert.Equal(expectedLeft, (BigInteger)(value << shift));
            Assert.Equal(expectedRight, (BigInteger)(value >> shift));

            var raw = IntegerTestHelpers.NormalizeUnsigned1024((BigInteger)value);
            var expectedLogicalRight = IntegerTestHelpers.NormalizeSigned1024(raw >> normalizedShift);
            Assert.Equal(expectedLogicalRight, (BigInteger)(value >>> shift));
            var expectedRotateLeft = IntegerTestHelpers.NormalizeSigned1024(
                IntegerTestHelpers.NormalizeUnsigned1024((raw << normalizedShift) | (raw >> (1024 - normalizedShift))));
            var expectedRotateRight = IntegerTestHelpers.NormalizeSigned1024(
                IntegerTestHelpers.NormalizeUnsigned1024((raw >> normalizedShift) | (raw << (1024 - normalizedShift))));

            Assert.Equal(expectedRotateLeft, (BigInteger)Int1024.RotateLeft(value, shift));
            Assert.Equal(expectedRotateRight, (BigInteger)Int1024.RotateRight(value, shift));
        }
    }

    /// <summary>
    /// Validates parse/try-parse/format round-trips across UTF-16 and UTF-8 overloads and confirms range failures.
    /// </summary>
    /// <remarks>
    /// Inputs are generated from valid <see cref="Int1024"/> instances, ensuring each representation can round-trip
    /// through text and UTF-8 APIs. Explicit out-of-range payloads verify <see cref="OverflowException"/> and
    /// <c>TryParse</c> failure behavior.
    /// </remarks>
    [Fact]
    public void ParseTryParseFormattingAndUtf8RoundTrip()
    {
        Span<char> charBuffer = stackalloc char[320];
        Span<byte> byteBuffer = stackalloc byte[320];

        var random = new Random(2020);
        for (var i = 0; i < 100; i++)
        {
            var value = IntegerTestHelpers.RandomInt1024(random);
            var text = value.ToString();

            Assert.Equal(value, Int1024.Parse(text, CultureInfo.InvariantCulture));
            Assert.True(Int1024.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed));
            Assert.Equal(value, parsed);

            var utf8 = Encoding.UTF8.GetBytes(text);
            Assert.Equal(value, Int1024.Parse(utf8, NumberStyles.Integer, CultureInfo.InvariantCulture));
            Assert.True(Int1024.TryParse(utf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed));
            Assert.Equal(value, parsed);

            Span<char> chars = charBuffer[..text.Length];
            Assert.True(value.TryFormat(chars, out var charsWritten, default, CultureInfo.InvariantCulture));
            Assert.Equal(text, chars[..charsWritten].ToString());

            Span<byte> bytes = byteBuffer[..text.Length];
            Assert.True(value.TryFormat(bytes, out var bytesWritten, default, CultureInfo.InvariantCulture));
            Assert.Equal(text, Encoding.UTF8.GetString(bytes[..bytesWritten]));
        }

        var outOfRangePositive = (IntegerTestHelpers.Int1024Max + 1).ToString(CultureInfo.InvariantCulture);
        var outOfRangeNegative = (IntegerTestHelpers.Int1024Min - 1).ToString(CultureInfo.InvariantCulture);
        Assert.Throws<OverflowException>(() => Int1024.Parse(outOfRangePositive, CultureInfo.InvariantCulture));
        Assert.Throws<OverflowException>(() => Int1024.Parse(outOfRangeNegative, CultureInfo.InvariantCulture));
        Assert.False(Int1024.TryParse(outOfRangePositive, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.False(Int1024.TryParse(outOfRangeNegative, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
    }

    /// <summary>
    /// Verifies formatting APIs honor the supplied provider for decimal/general output.
    /// </summary>
    [Fact]
    public void Formatting_DecimalAndGeneral_UseProvidedCultureTokens()
    {
        var provider = (CultureInfo)CultureInfo.InvariantCulture.Clone();
        provider.NumberFormat.NegativeSign = "~";

        var value = (Int1024)(-123);
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
        Assert.Equal(Int1024.MinValue, Int1024.MinValue / Int1024.One);
        Assert.Equal(Int1024.Zero, Int1024.MinValue % Int1024.One);

        Assert.Throws<OverflowException>(() => checked(Int1024.MinValue / (Int1024)(-1)));

        Assert.Throws<OverflowException>(() => _ = Int1024.MinValue / -1);
        Assert.Throws<OverflowException>(() => _ = Int1024.MinValue % -1);
    }

    /// <summary>
    /// Validates explicit conversions from <see cref="UInt1024"/> to <see cref="Int1024"/> in unchecked and checked
    /// contexts.
    /// </summary>
    [Fact]
    public void ExplicitConversion_FromUInt1024_HonorsCheckedSemantics()
    {
        var inRangeUnsigned = (UInt1024)Int1024.MaxValue;
        var convertedInRange = (Int1024)inRangeUnsigned;
        Assert.Equal(Int1024.MaxValue, convertedInRange);
        Assert.Equal(Int1024.MaxValue, checked((Int1024)inRangeUnsigned));

        var wrapsToNegativeOne = UInt1024.MaxValue;
        Assert.Equal(-1, (Int1024)wrapsToNegativeOne);
        Assert.Throws<OverflowException>(() => checked((Int1024)wrapsToNegativeOne));

        var signBitOnly = new UInt1024((UInt512)1 << 511, 0);
        Assert.Equal(Int1024.MinValue, (Int1024)signBitOnly);
        Assert.Throws<OverflowException>(() => checked((Int1024)signBitOnly));
    }

    /// <summary>
    /// Validates style-sensitive parsing edges and sign handling for decimal and hexadecimal forms.
    /// </summary>
    [Fact]
    public void ParseAndTryParse_RespectStylesAndRejectMalformedInput()
    {
        const string maxHex = "7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
        Assert.Equal(Int1024.MaxValue, Int1024.Parse(maxHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));

        const string minHexTwosComplement = "8000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
        Assert.Equal(Int1024.MinValue, Int1024.Parse(minHexTwosComplement, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));

        Assert.True(Int1024.TryParse("  -17  ", NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed));
        Assert.Equal(-17, parsed);

        Assert.False(Int1024.TryParse("--1", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.False(Int1024.TryParse("0x10", NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out _));
    }


    /// <summary>
    /// Exercises convenience parse/try-parse overloads that use default style/provider parameters.
    /// </summary>
    [Fact]
    public void ParseAndTryParse_ConvenienceOverloads_AreCovered()
    {
        const string decimalText = "-340282366920938463463374607431768211456";
        ReadOnlySpan<char> decimalSpan = decimalText.AsSpan();
        ReadOnlySpan<byte> decimalUtf8 = Encoding.UTF8.GetBytes(decimalText);

        var expected = (Int1024)(-(BigInteger.One << 128));

        Assert.Equal(expected, Int1024.Parse(decimalText));
        Assert.Equal(expected, Int1024.Parse(decimalText, CultureInfo.InvariantCulture));
        Assert.Equal(expected, Int1024.Parse(decimalSpan, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.True(Int1024.TryParse(decimalText, out var fromString));
        Assert.Equal(expected, fromString);

        Assert.True(Int1024.TryParse(decimalSpan, out var fromSpan));
        Assert.Equal(expected, fromSpan);

        Assert.True(Int1024.TryParse(decimalUtf8, out var fromUtf8));
        Assert.Equal(expected, fromUtf8);
    }

    /// <summary>
    /// Verifies hexadecimal parsing follows signed two's-complement semantics at 1024-bit boundaries.
    /// </summary>
    [Fact]
    public void Parse_HexBoundaryValues_MapToExpectedSignedResults()
    {
        const string allOnesHex = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
        Assert.Equal(-1, Int1024.Parse(allOnesHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.True(Int1024.TryParse(allOnesHex.AsSpan(), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var parsed));
        Assert.Equal(-1, parsed);

        const string minPlusOneHex = "8000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001";
        Assert.Equal(Int1024.MinValue + Int1024.One, Int1024.Parse(minPlusOneHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));

        var utf8 = Encoding.UTF8.GetBytes(minPlusOneHex);
        Assert.True(Int1024.TryParse(utf8, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var parsedUtf8));
        Assert.Equal(Int1024.MinValue + Int1024.One, parsedUtf8);
    }

    /// <summary>
    /// Verifies explicit conversion operators for both successful in-range casts and overflow-protected casts.
    /// </summary>
    /// <remarks>
    /// Cases include narrowing signed casts, signed-to-unsigned rejection for negative values, and conversion from
    /// <see cref="Int1024"/> to <see cref="BigInteger"/> through <see cref="UInt1024"/> for positive values.
    /// </remarks>
    [Fact]
    public void ConversionOperators_CheckedAndUncheckedBehaviors_AreCorrect()
    {
        Assert.Equal(42L, (long)(Int1024)42);
        Assert.Equal(-42, (Int512)(Int1024)(-42));
        Assert.Equal(unchecked((byte)-1), (byte)(Int1024)(-1));
        Assert.Equal(unchecked((sbyte)-1), (sbyte)(Int1024)(-1));
        Assert.Equal(UInt512.MaxValue, (UInt512)(Int1024)(-1));

        var largePositive = (Int1024)(new BigInteger(1) << 200);
        Assert.Equal(0L, (long)largePositive);
        Assert.Equal((Int512)(new BigInteger(1) << 200), (Int512)largePositive);
        Assert.Equal((Int512)(new BigInteger(1) << 200), checked((Int512)largePositive));
        Assert.Throws<OverflowException>(() => _ = checked((long)largePositive));

        var fitsInt512 = (Int1024)(new BigInteger(1) << 300);
        Assert.Equal((Int512)(new BigInteger(1) << 300), checked((Int512)fitsInt512));

        var tooLargeForInt512 = (Int1024)(new BigInteger(1) << 700);
        Assert.Throws<OverflowException>(() => _ = checked((Int512)tooLargeForInt512));
        Assert.Throws<OverflowException>(() => _ = checked((sbyte)largePositive));
        Assert.Throws<OverflowException>(() => _ = checked((ulong)(Int1024)(-1)));

        var smallNegative = (Int1024)(-42);
        Assert.Equal((sbyte)-42, (sbyte)smallNegative);
        Assert.Equal((sbyte)-42, checked((sbyte)smallNegative));

        var negative = (Int1024)(-1);
        Assert.Equal(UInt1024.MaxValue, (UInt1024)negative);
        Assert.Throws<OverflowException>(() => _ = checked((UInt1024)negative));

        var positive = (Int1024)123;
        Assert.Equal(123, (BigInteger)(UInt1024)positive);
        Assert.Equal((UInt1024)positive, checked((UInt1024)positive));

        Assert.Equal(Int1024.MaxValue, checked((Int1024)IntegerTestHelpers.Int1024Max));
        Assert.Equal(Int1024.MinValue, checked((Int1024)IntegerTestHelpers.Int1024Min));

        var aboveMax = IntegerTestHelpers.Int1024Max + BigInteger.One;
        Assert.Equal(Int1024.MinValue, (Int1024)aboveMax);
        Assert.Throws<OverflowException>(() => _ = checked((Int1024)aboveMax));

        var belowMin = IntegerTestHelpers.Int1024Min - BigInteger.One;
        Assert.Equal(Int1024.MaxValue, (Int1024)belowMin);
        Assert.Throws<OverflowException>(() => _ = checked((Int1024)belowMin));

        Assert.Equal(Int1024.MinValue, checked((Int1024)IntegerTestHelpers.Int1024Min));
        Assert.Equal(Int1024.MaxValue, checked((Int1024)IntegerTestHelpers.Int1024Max));
    }

    /// <summary>
    /// Verifies checked <see cref="BigInteger"/> to <see cref="Int1024"/> conversion succeeds for in-range values.
    /// </summary>
    [Fact]
    public void CheckedBigIntegerConversion_InRangeValues_Succeeds()
    {
        Assert.Equal(Int1024.Zero, checked((Int1024)BigInteger.Zero));
        Assert.Equal(Int1024.One, checked((Int1024)BigInteger.One));
        Assert.Equal((Int1024)(-1), checked((Int1024)BigInteger.MinusOne));

        var midPositive = (BigInteger.One << 200) + 123456789;
        Assert.Equal((Int1024)midPositive, checked((Int1024)midPositive));

        var midNegative = -((BigInteger.One << 200) + 987654321);
        Assert.Equal((Int1024)midNegative, checked((Int1024)midNegative));

        Assert.Equal(Int1024.MaxValue, checked((Int1024)IntegerTestHelpers.Int1024Max));
        Assert.Equal(Int1024.MinValue, checked((Int1024)IntegerTestHelpers.Int1024Min));
    }

    /// <summary>
    /// Verifies that boundary shift and rotate counts that are multiples of 1024 behave as identity operations.
    /// </summary>
    /// <remarks>
    /// This test pins down count normalization behavior for exact-width and large-magnitude counts, including
    /// negative values, where the implementation masks counts with <c>0x3FF</c>.
    /// </remarks>
    [Fact]
    public void ShiftAndRotate_CountsEquivalentToZero_AreIdentity()
    {
        var value = (Int1024)(new BigInteger(1) << 200) - 12345;

        foreach (var shift in new[] { 0, 1024, 1024, -1024, -1024 })
        {
            Assert.Equal(value, value << shift);
            Assert.Equal(value, value >> shift);
            Assert.Equal(value, value >>> shift);
            Assert.Equal(value, Int1024.RotateLeft(value, shift));
            Assert.Equal(value, Int1024.RotateRight(value, shift));
        }
    }

    /// <summary>
    /// Verifies division edge-case behavior for zero divisors and the checked <c>MinValue / -1</c> overflow path.
    /// </summary>
    [Fact]
    public void DivisionContracts_HandleZeroAndCheckedOverflowEdges()
    {
        Assert.Throws<DivideByZeroException>(() => _ = Int1024.One / Int1024.Zero);
        Assert.Throws<DivideByZeroException>(() => _ = Int1024.One % Int1024.Zero);
        Assert.Throws<OverflowException>(() => checked(Int1024.MinValue / (Int1024)(-1)));
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

        Assert.Throws<FormatException>(() => Int1024.Parse("".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Throws<FormatException>(() => Int1024.Parse(ReadOnlySpan<byte>.Empty, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.False(Int1024.TryParse(emptyChars, NumberStyles.Integer, CultureInfo.InvariantCulture, out var charResult));
        Assert.Equal(Int1024.Zero, charResult);

        Assert.False(Int1024.TryParse(emptyUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out var utf8Result));
        Assert.Equal(Int1024.Zero, utf8Result);
    }

    /// <summary>
    /// Verifies fixed-size formatting APIs report failure when destinations are too small.
    /// </summary>
    [Fact]
    public void TryFormat_WithInsufficientDestination_FailsWithoutPartialWrite()
    {
        var value = Int1024.MinValue;
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
        Assert.Equal(0, Int1024.Zero.GetShortestBitLength());
        Assert.Equal(1, ((Int1024)(-1)).GetShortestBitLength());
        Assert.Equal(2, ((Int1024)(-2)).GetShortestBitLength());
        Assert.Equal(7, ((Int1024)127).GetShortestBitLength());
        Assert.Equal(8, ((Int1024)128).GetShortestBitLength());

        Assert.Equal(128, Int1024.Zero.GetByteCount());
        Assert.Equal(128, ((Int1024)(-1)).GetByteCount());
        Assert.Equal(128, ((Int1024)127).GetByteCount());
        Assert.Equal(128, ((Int1024)128).GetByteCount());

        Assert.Equal(0, Int1024.Log2(Int1024.Zero));
        Assert.Equal(7, Int1024.Log2(128));
        Assert.Throws<ArgumentOutOfRangeException>(() => Int1024.Log2(-1));
    }

    /// <summary>
    /// Verifies bit-count helper accuracy for leading zeros, trailing zeros, and population count.
    /// </summary>
    /// <remarks>
    /// Expected values are computed from the canonical unsigned 1024-bit pattern corresponding to each signed
    /// sample, ensuring these helpers are validated as bit operations rather than sign-aware arithmetic operations.
    /// </remarks>
    [Fact]
    public void LeadingTrailingAndPopCount_AreBitAccurate()
    {
        Assert.Equal(1024, Int1024.LeadingZeroCount(Int1024.Zero));
        Assert.Equal(1024, Int1024.TrailingZeroCount(Int1024.Zero));
        Assert.Equal(0, Int1024.PopCount(Int1024.Zero));

        var random = new Random(9001);
        for (var i = 0; i < 200; i++)
        {
            var value = IntegerTestHelpers.RandomInt1024(random);
            var raw = IntegerTestHelpers.NormalizeUnsigned1024((BigInteger)value);

            var expectedLeading = raw.IsZero ? 1024 : 1024 - (int)BigInteger.Log2(raw) - 1;

            var expectedTrailing = 0;
            if (raw.IsZero)
            {
                expectedTrailing = 1024;
            }
            else
            {
                while (((raw >> expectedTrailing) & BigInteger.One) == BigInteger.Zero)
                {
                    expectedTrailing++;
                }
            }

            var expectedPop = 0;
            for (var bit = 0; bit < 1024; bit++)
            {
                if (((raw >> bit) & BigInteger.One) != BigInteger.Zero)
                {
                    expectedPop++;
                }
            }

            Assert.Equal(expectedLeading, Int1024.LeadingZeroCount(value));
            Assert.Equal(expectedTrailing, Int1024.TrailingZeroCount(value));
            Assert.Equal(expectedPop, Int1024.PopCount(value));
        }
    }

    /// <summary>
    /// Verifies arithmetic right shift preserves sign for negative values under high and normalized shift counts.
    /// </summary>
    [Fact]
    public void RightShift_OnNegativeValues_PerformsSignExtension()
    {
        var allOnes = (Int1024)(-1);
        foreach (var shift in new[] { 1, 63, 127, 128, 255, 1024, -1, -257 })
        {
            Assert.Equal(allOnes, allOnes >> shift);
        }

        var min = Int1024.MinValue;
        Assert.Equal(-(BigInteger.One << 512), (BigInteger)(min >> 511));
        Assert.Equal(min, min >> 1024);
    }

    /// <summary>
    /// Verifies parsing in hexadecimal mode honors two's-complement interpretation within 1024-bit width boundaries.
    /// </summary>
    [Fact]
    public void Parse_HexTwoComplementBoundaries_AndOverwideInput_UseExpectedContracts()
    {
        const string negativeOneHex = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
        Assert.Equal(-1, Int1024.Parse(negativeOneHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));

        const string minHex = "8000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
        Assert.Equal(Int1024.MinValue, Int1024.Parse(minHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));

        var overwideHex = new string('F', 257);
        Assert.False(Int1024.TryParse(overwideHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out _));
        Assert.Throws<OverflowException>(() => Int1024.Parse(overwideHex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
    }
}
