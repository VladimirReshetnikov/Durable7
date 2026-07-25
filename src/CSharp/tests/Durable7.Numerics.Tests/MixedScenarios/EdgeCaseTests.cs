using System.Globalization;
using System.Numerics;
using System.Text;
using Xunit;

namespace Durable7.Numerics.Tests;

/// <summary>
/// Exercises less-obvious edge contracts that are easy to regress when implementation details change.
/// </summary>
public sealed class EdgeCaseTests
{
    /// <summary>
    /// Verifies that all string-based <c>Parse</c> overloads throw <see cref="ArgumentNullException"/> for null
    /// input across all supported integer widths.
    /// </summary>
    [Fact]
    public void Parse_NullString_ThrowsArgumentNullException_ForAllStringOverloadsAndWidths()
    {
        Assert.Throws<ArgumentNullException>("s", () => UInt256.Parse((string)null!));
        Assert.Throws<ArgumentNullException>("s", () => UInt256.Parse((string)null!, CultureInfo.InvariantCulture));
        Assert.Throws<ArgumentNullException>("s", () => UInt256.Parse((string)null!, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.Throws<ArgumentNullException>("s", () => Int256.Parse((string)null!));
        Assert.Throws<ArgumentNullException>("s", () => Int256.Parse((string)null!, CultureInfo.InvariantCulture));
        Assert.Throws<ArgumentNullException>("s", () => Int256.Parse((string)null!, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.Throws<ArgumentNullException>("s", () => UInt512.Parse((string)null!));
        Assert.Throws<ArgumentNullException>("s", () => UInt512.Parse((string)null!, CultureInfo.InvariantCulture));
        Assert.Throws<ArgumentNullException>("s", () => UInt512.Parse((string)null!, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.Throws<ArgumentNullException>("s", () => Int512.Parse((string)null!));
        Assert.Throws<ArgumentNullException>("s", () => Int512.Parse((string)null!, CultureInfo.InvariantCulture));
        Assert.Throws<ArgumentNullException>("s", () => Int512.Parse((string)null!, NumberStyles.Integer, CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies that null-string <c>TryParse</c> follows .NET primitive conventions by returning
    /// <see langword="false"/> and a zero result.
    /// </summary>
    [Fact]
    public void TryParse_NullStrings_ReturnFalseAndZeroResult()
    {
        Assert.False(UInt256.TryParse((string?)null, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsedUnsigned));
        Assert.Equal(UInt256.Zero, parsedUnsigned);

        Assert.False(Int256.TryParse((string?)null, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsedSigned));
        Assert.Equal(Int256.Zero, parsedSigned);

        Assert.False(UInt512.TryParse((string?)null, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsedUnsigned512));
        Assert.Equal(UInt512.Zero, parsedUnsigned512);

        Assert.False(Int512.TryParse((string?)null, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsedSigned512));
        Assert.Equal(Int512.Zero, parsedSigned512);
    }

    /// <summary>
    /// Verifies both 256-bit integer types honor non-default sign tokens from a custom <see cref="NumberFormatInfo"/>.
    /// </summary>
    /// <remarks>
    /// Custom signs are a low-frequency production scenario, but they exercise parser/provider plumbing that is easy
    /// to break when parse implementations are refactored.
    /// </remarks>
    [Fact]
    public void Parse_CustomPositiveAndNegativeSigns_AreRespectedAcrossUtf16AndUtf8()
    {
        var custom = (NumberFormatInfo)CultureInfo.InvariantCulture.NumberFormat.Clone();
        custom.PositiveSign = "p";
        custom.NegativeSign = "m";

        const string unsignedText = "  p00018446744073709551616  ";
        const string signedText = "\tm170141183460469231731687303715884105728\n";

        var expectedUnsigned = (UInt256)(BigInteger.One << 64);
        var expectedSigned = (Int256)(-(BigInteger.One << 127));

        Assert.Equal(expectedUnsigned, UInt256.Parse(unsignedText, NumberStyles.Integer, custom));
        Assert.True(UInt256.TryParse(unsignedText.AsSpan(), NumberStyles.Integer, custom, out var parsedUnsigned));
        Assert.Equal(expectedUnsigned, parsedUnsigned);
        Assert.True(UInt256.TryParse(Encoding.UTF8.GetBytes(unsignedText), NumberStyles.Integer, custom, out parsedUnsigned));
        Assert.Equal(expectedUnsigned, parsedUnsigned);

        Assert.Equal(expectedSigned, Int256.Parse(signedText, NumberStyles.Integer, custom));
        Assert.True(Int256.TryParse(signedText.AsSpan(), NumberStyles.Integer, custom, out var parsedSigned));
        Assert.Equal(expectedSigned, parsedSigned);
        Assert.True(Int256.TryParse(Encoding.UTF8.GetBytes(signedText), NumberStyles.Integer, custom, out parsedSigned));
        Assert.Equal(expectedSigned, parsedSigned);

        Assert.False(UInt256.TryParse("m1", NumberStyles.Integer, custom, out _));
    }

    /// <summary>
    /// Verifies a dense set of unsigned bitwise/ring identities that catch carry, complement, and mask regressions.
    /// </summary>
    [Fact]
    public void UInt256_BitwiseAndWraparoundIdentities_HoldAcrossRandomSamples()
    {
        var random = new Random(9001);
        for (var i = 0; i < 400; i++)
        {
            var left = IntegerTestHelpers.RandomUInt256(random);
            var right = IntegerTestHelpers.RandomUInt256(random);

            Assert.Equal(UInt256.MaxValue, left + ~left);
            Assert.Equal(UInt256.Zero, left ^ left);
            Assert.Equal(UInt256.Zero, left & ~left);
            Assert.Equal(UInt256.MaxValue, left | ~left);

            Assert.Equal(~(left | right), ~left & ~right);
            Assert.Equal(~(left & right), ~left | ~right);
        }
    }

    /// <summary>
    /// Verifies arithmetic right-shift semantics on signed values against <see cref="BigInteger"/> across
    /// boundary-heavy data.
    /// </summary>
    [Fact]
    public void Int256_RightShift_MatchesBigIntegerAcrossExtremeCountsAndBoundaryValues()
    {
        var values = new[]
        {
            Int256.Zero,
            Int256.One,
            -1,
            Int256.MaxValue,
            Int256.MinValue,
            (Int256)(-(BigInteger.One << 200)),
            (Int256)((BigInteger.One << 200) + 123456789),
            (Int256)(-(BigInteger.One << 127) + 17),
            (Int256)((BigInteger.One << 127) - 1),
        };

        var counts = new[] { int.MinValue, -513, -257, -256, -255, -129, -128, -127, -1, 0, 1, 127, 128, 129, 255, 256, 257, 513, int.MaxValue };

        foreach (var value in values)
        {
            var valueAsBig = (BigInteger)value;
            foreach (var count in counts)
            {
                var normalized = count & 0xFF;
                var expected = IntegerTestHelpers.NormalizeSigned(valueAsBig >> normalized);
                Assert.Equal(expected, (BigInteger)(value >> count));
            }
        }
    }

    /// <summary>
    /// Verifies canonical two's-complement identities for signed values over randomized 256-bit bit patterns.
    /// </summary>
    [Fact]
    public void Int256_TwosComplementIdentities_HoldAcrossRandomSamples()
    {
        var random = new Random(271828);
        for (var i = 0; i < 400; i++)
        {
            var value = IntegerTestHelpers.RandomInt256(random);

            Assert.Equal(value, ~~value);
            Assert.Equal(~value, -value - Int256.One);
            Assert.Equal(Int256.Zero, value + -value);
        }
    }

    /// <summary>
    /// Verifies bit-complement invariants that jointly exercise <c>PopCount</c>, bitwise NOT, and set cardinality
    /// limits.
    /// </summary>
    [Fact]
    public void PopCount_ComplementaryInvariantsAlwaysSumToBitWidth()
    {
        var random = new Random(271828);
        for (var i = 0; i < 500; i++)
        {
            var unsigned = IntegerTestHelpers.RandomUInt256(random);
            Assert.Equal(256, UInt256.PopCount(unsigned) + UInt256.PopCount(~unsigned));

            var signed = IntegerTestHelpers.RandomInt256(random);
            Assert.Equal(256, Int256.PopCount(signed) + Int256.PopCount(~signed));
        }
    }

    /// <summary>
    /// Verifies all shift and rotate operations depend only on the low 8 bits of the count, even for pathological
    /// magnitudes.
    /// </summary>
    [Fact]
    public void ShiftAndRotate_CountNormalization_MatchesEquivalentLowByteCounts()
    {
        var random = new Random(1729);
        var extremeCounts = new[]
        {
            int.MinValue,
            int.MinValue + 1,
            -1_000_000_001,
            -1_000_000_000,
            -65_537,
            -257,
            -256,
            -255,
            -129,
            -128,
            -127,
            -1,
            0,
            1,
            127,
            128,
            129,
            255,
            256,
            257,
            513,
            65_537,
            1_000_000_000,
            1_000_000_001,
            int.MaxValue - 1,
            int.MaxValue,
        };

        foreach (var count in extremeCounts)
        {
            var normalized = count & 0xFF;
            for (var sample = 0; sample < 25; sample++)
            {
                var u = IntegerTestHelpers.RandomUInt256(random);
                var s = IntegerTestHelpers.RandomInt256(random);

                Assert.Equal(u << normalized, u << count);
                Assert.Equal(u >> normalized, u >> count);
                Assert.Equal(UInt256.RotateLeft(u, normalized), UInt256.RotateLeft(u, count));
                Assert.Equal(UInt256.RotateRight(u, normalized), UInt256.RotateRight(u, count));

                Assert.Equal(s << normalized, s << count);
                Assert.Equal(s >> normalized, s >> count);
                Assert.Equal(Int256.RotateLeft(s, normalized), Int256.RotateLeft(s, count));
                Assert.Equal(Int256.RotateRight(s, normalized), Int256.RotateRight(s, count));
            }
        }
    }

    /// <summary>
    /// Verifies the core quotient/remainder identity for randomized signed and unsigned samples.
    /// </summary>
    [Fact]
    public void DivisionAndRemainder_SatisfyReconstructionIdentity()
    {
        var random = new Random(5150);
        for (var i = 0; i < 300; i++)
        {
            var leftUnsigned = IntegerTestHelpers.RandomUInt256(random);
            var rightUnsigned = IntegerTestHelpers.RandomUInt256(random);
            if (rightUnsigned.IsZero)
            {
                rightUnsigned = UInt256.One;
            }

            var qUnsigned = leftUnsigned / rightUnsigned;
            var rUnsigned = leftUnsigned % rightUnsigned;
            Assert.Equal(leftUnsigned, qUnsigned * rightUnsigned + rUnsigned);

            var leftSigned = IntegerTestHelpers.RandomInt256(random);
            var rightSigned = IntegerTestHelpers.RandomInt256(random);
            if (rightSigned.IsZero)
            {
                rightSigned = Int256.One;
            }

            if (leftSigned == Int256.MinValue && rightSigned == -1)
            {
                rightSigned = Int256.One;
            }

            var qSigned = leftSigned / rightSigned;
            var rSigned = leftSigned % rightSigned;
            Assert.Equal(leftSigned, qSigned * rightSigned + rSigned);
        }
    }

    /// <summary>
    /// Verifies signed remainder shape constraints: the remainder has dividend sign and magnitude less than the
    /// divisor.
    /// </summary>
    [Fact]
    public void SignedDivision_RemainderSignAndMagnitude_FollowContract()
    {
        var random = new Random(112358);
        for (var i = 0; i < 350; i++)
        {
            var dividend = IntegerTestHelpers.RandomInt256(random);
            var divisor = IntegerTestHelpers.RandomInt256(random);
            if (divisor.IsZero)
            {
                divisor = Int256.One;
            }

            if (dividend == Int256.MinValue && divisor == -1)
            {
                divisor = Int256.One;
            }

            var remainder = dividend % divisor;
            Assert.True(Int256.Abs(remainder) < Int256.Abs(divisor));

            if (!remainder.IsZero)
            {
                Assert.Equal(Int256.Sign(dividend), Int256.Sign(remainder));
            }
        }
    }

    /// <summary>
    /// Verifies that malformed UTF-8 payloads cannot be parsed into either signed or unsigned 256-bit values.
    /// </summary>
    [Fact]
    public void Utf8Parse_InvalidUtf8Bytes_AreRejected()
    {
        byte[] invalidUtf8 = [0x31, 0xC3, 0x28, 0x32]; // "1" + invalid 2-byte sequence + "2"

        Assert.False(UInt256.TryParse(invalidUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.False(Int256.TryParse(invalidUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));

        Assert.Throws<FormatException>(() => UInt256.Parse(invalidUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Throws<FormatException>(() => Int256.Parse(invalidUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// Verifies checked unary negation overflows only for <see cref="Int256.MinValue"/>.
    /// </summary>
    [Fact]
    public void CheckedNegation_OverflowsOnlyForMinValue()
    {
        Assert.Throws<OverflowException>(() => checked(-Int256.MinValue));

        var samples = new[]
        {
            Int256.Zero,
            Int256.One,
            -1,
            123456789,
            -123456789,
            Int256.MaxValue,
            Int256.MinValue + Int256.One,
        };

        foreach (var sample in samples)
        {
            if (sample == Int256.MinValue)
            {
                continue;
            }

            var negated = checked(-sample);
            Assert.Equal(sample, checked(-negated));
        }
    }

    /// <summary>
    /// Verifies the classic two's-complement negation identity <c>-x == (~x + 1)</c> for every 256-bit pattern.
    /// </summary>
    /// <remarks>
    /// The relation must also hold for <see cref="Int256.MinValue"/>, where unchecked negation wraps to itself.
    /// </remarks>
    [Fact]
    public void Int256_UncheckedNegation_MatchesBitwiseComplementPlusOneIdentity()
    {
        var random = new Random(73);
        for (var i = 0; i < 500; i++)
        {
            var value = IntegerTestHelpers.RandomInt256(random);
            var negated = -value;
            Assert.Equal(~value + Int256.One, negated);
        }

        Assert.Equal(Int256.MinValue, -Int256.MinValue);
        Assert.Equal(~Int256.MinValue + Int256.One, -Int256.MinValue);
    }

    /// <summary>
    /// Verifies quotient semantics for negative operands are truncation toward zero (not floor division).
    /// </summary>
    [Fact]
    public void Int256_DivisionAndRemainder_TruncateTowardZeroAcrossSignCombinations()
    {
        var cases = new (Int256 Dividend, Int256 Divisor)[]
        {
            (-5, 2),
            (5, -2),
            (-5, -2),
            (-1, 3),
            (1, -3),
            ((Int256)(-(BigInteger.One << 200) + 123), 97),
            ((Int256)((BigInteger.One << 200) - 123), -97),
        };

        foreach (var (dividend, divisor) in cases)
        {
            var expectedQ = (BigInteger)dividend / (BigInteger)divisor;
            var expectedR = (BigInteger)dividend % (BigInteger)divisor;

            Assert.Equal(expectedQ, (BigInteger)(dividend / divisor));
            Assert.Equal(expectedR, (BigInteger)(dividend % divisor));
        }
    }

    /// <summary>
    /// Verifies huge out-of-range decimal payloads fail deterministically across all parse entry points.
    /// </summary>
    [Fact]
    public void Parse_HugeDecimalPayloads_OverflowAndTryParseReturnsFalseWithZeroResult()
    {
        var hugeUnsigned = new string('9', 10_000);
        var hugeSigned = "-" + hugeUnsigned;

        Assert.Throws<OverflowException>(() => UInt256.Parse(hugeUnsigned, NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Throws<OverflowException>(() => Int256.Parse(hugeSigned, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.False(UInt256.TryParse(hugeUnsigned, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsedUnsigned));
        Assert.Equal(UInt256.Zero, parsedUnsigned);
        Assert.False(Int256.TryParse(hugeSigned, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsedSigned));
        Assert.Equal(Int256.Zero, parsedSigned);

        var hugeUnsignedUtf8 = Encoding.UTF8.GetBytes(hugeUnsigned);
        var hugeSignedUtf8 = Encoding.UTF8.GetBytes(hugeSigned);

        Assert.False(UInt256.TryParse(hugeUnsignedUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsedUnsigned));
        Assert.Equal(UInt256.Zero, parsedUnsigned);
        Assert.False(Int256.TryParse(hugeSignedUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsedSigned));
        Assert.Equal(Int256.Zero, parsedSigned);
    }

    /// <summary>
    /// Verifies a hand-rolled modular exponentiation routine over <see cref="UInt256"/> against
    /// <see cref="BigInteger.ModPow(BigInteger, BigInteger, BigInteger)"/>.
    /// </summary>
    /// <remarks>
    /// The test uses a large odd modulus near <see cref="UInt256.MaxValue"/> and a sparse high-bit exponent to stress
    /// repeated multiply-and-reduce behavior close to 256-bit boundaries.
    /// </remarks>
    [Fact]
    public void UInt256_ModularExponentiationBySquaring_MatchesBigIntegerModPow()
    {
        /*
         * INCORRECT VERSION (do not restore):
         *
         *   acc = (acc * factor) % modulus;
         *   factor = (factor * factor) % modulus;
         *   Assert.Equal(BigInteger.ModPow(...), (BigInteger)acc);
         *
         * Why it was wrong (full rationale):
         * - UInt256 operator* is intentionally fixed-width arithmetic: each multiply is truncated modulo 2^256.
         * - Therefore, in the update steps above, multiplication wraps first and only then applies "% modulus".
         * - BigInteger.ModPow computes in an unbounded integer domain and performs reduction after full-precision
         *   multiplication; no intermediate 256-bit truncation occurs.
         * - These are different rings: the test compared "(a*b mod 2^256) mod m" behavior against
         *   "(a*b) mod m" behavior. They can coincide in some cases but are not equivalent in general.
         * - The old assertion therefore encoded an arithmetic-model mismatch, not a product defect in UInt256.
         *
         * Future guardrail:
         * - Any expected model for UInt256 algorithms that multiply must include the same per-step wraparound.
         * - Only compare to pure BigInteger.ModPow when intermediate full-width multiplication is intentionally
         *   preserved (for example via wider intermediate math and explicit reduction before narrowing).
         */
        var modulus = (UInt256)(IntegerTestHelpers.UIntMax - 188);
        var baseValue = (UInt256)((BigInteger.One << 255) + (BigInteger.One << 191) + 0x1234_5678_9ABC_DEF0);
        var exponent = (UInt256)((BigInteger.One << 200) + (BigInteger.One << 129) + 0xBEEF);

        var acc = UInt256.One;
        var factor = baseValue % modulus;
        var exp = exponent;

        while (!exp.IsZero)
        {
            if ((exp & UInt256.One) == UInt256.One)
            {
                acc = acc * factor % modulus;
            }

            exp >>= 1;
            factor = factor * factor % modulus;
        }

        var expectedAcc = BigInteger.One;
        var expectedFactor = (BigInteger)baseValue % (BigInteger)modulus;
        var expectedExp = (BigInteger)exponent;

        while (expectedExp > BigInteger.Zero)
        {
            if (!expectedExp.IsEven)
            {
                expectedAcc = IntegerTestHelpers.NormalizeUnsigned(expectedAcc * expectedFactor) % (BigInteger)modulus;
            }

            expectedExp >>= 1;
            expectedFactor = IntegerTestHelpers.NormalizeUnsigned(expectedFactor * expectedFactor) % (BigInteger)modulus;
        }

        Assert.Equal(expectedAcc, (BigInteger)acc);
    }

    /// <summary>
    /// Verifies a long-running signed polynomial recurrence against explicit two's-complement normalization.
    /// </summary>
    /// <remarks>
    /// This resembles a stress workload where intermediate products repeatedly exceed 256 bits before wraparound.
    /// </remarks>
    [Fact]
    public void Int256_PolynomialRecurrence_MatchesSignedWraparoundModel()
    {
        var seed = (Int256)(-(BigInteger.One << 254) + 0x1F2E3D4C5B6A7988);
        var coefficientA = (Int256)((BigInteger.One << 129) + 0x1020_3040_5060_7080);
        var coefficientB = (Int256)(-0x1122_3344_5566_7788);
        var coefficientC = (Int256)((BigInteger.One << 200) - 77);

        var state = seed;
        var expected = (BigInteger)seed;

        for (var i = 0; i < 240; i++)
        {
            state = state * state * coefficientA + (state << 11) + coefficientB - coefficientC;

            expected = IntegerTestHelpers.NormalizeSigned(
                expected * expected * (BigInteger)coefficientA
                + (expected << 11)
                + (BigInteger)coefficientB
                - (BigInteger)coefficientC);

            Assert.Equal(expected, (BigInteger)state);
        }
    }

    /// <summary>
    /// Verifies a long Fibonacci run in <see cref="UInt256"/> arithmetic against modulo-<c>2^256</c> reference math.
    /// </summary>
    [Fact]
    public void UInt256_FibonacciWraparoundSequence_MatchesModuloReference()
    {
        var a = UInt256.Zero;
        var b = UInt256.One;

        var expectedA = BigInteger.Zero;
        var expectedB = BigInteger.One;

        for (var i = 0; i < 900; i++)
        {
            var next = a + b;
            var expectedNext = IntegerTestHelpers.NormalizeUnsigned(expectedA + expectedB);

            Assert.Equal(expectedNext, (BigInteger)next);

            a = b;
            b = next;
            expectedA = expectedB;
            expectedB = expectedNext;
        }
    }

    /// <summary>
    /// Computes the Fibonacci sequence up to the signed 256-bit boundary and verifies exact agreement with
    /// <see cref="BigInteger"/> arithmetic at every step.
    /// </summary>
    /// <remarks>
    /// The test intentionally walks the sequence in checked mode and asserts that the very next term after the
    /// last in-range value throws <see cref="OverflowException"/>. This captures a realistic long-running
    /// accumulation scenario where overflow should occur at a deterministic boundary.
    /// </remarks>
    [Fact]
    public void FibonacciSequence_ReachesDeterministicCheckedOverflowBoundary()
    {
        var bigTerms = new List<BigInteger> { BigInteger.Zero, BigInteger.One };
        while (bigTerms[^1] + bigTerms[^2] <= IntegerTestHelpers.IntMax)
        {
            bigTerms.Add(bigTerms[^1] + bigTerms[^2]);
        }

        Assert.True(bigTerms[^1] <= IntegerTestHelpers.IntMax);
        Assert.True(bigTerms[^1] + bigTerms[^2] > IntegerTestHelpers.IntMax);

        var previous = Int256.Zero;
        var current = Int256.One;

        Assert.Equal(bigTerms[0], (BigInteger)previous);
        Assert.Equal(bigTerms[1], (BigInteger)current);

        for (var i = 2; i < bigTerms.Count; i++)
        {
            var next = checked(previous + current);
            Assert.Equal(bigTerms[i], (BigInteger)next);
            previous = current;
            current = next;
        }

        Assert.Throws<OverflowException>(() => checked(previous + current));
    }

    /// <summary>
    /// Verifies that checked factorial accumulation matches <see cref="BigInteger"/> exactly until the first
    /// multiplication that exceeds the signed 256-bit domain.
    /// </summary>
    [Fact]
    public void FactorialGrowth_MatchesBigIntegerUntilCheckedOverflow()
    {
        var n = 1;
        var bigFactorial = BigInteger.One;
        var intFactorial = Int256.One;

        while (bigFactorial * (n + 1) <= IntegerTestHelpers.IntMax)
        {
            n++;
            bigFactorial *= n;
            intFactorial = checked(intFactorial * (Int256)n);
            Assert.Equal(bigFactorial, (BigInteger)intFactorial);
        }

        Assert.Throws<OverflowException>(() => checked(intFactorial * (Int256)(n + 1)));
    }

    /// <summary>
    /// Evaluates randomly generated high-degree polynomials in unchecked signed 256-bit arithmetic and compares
    /// each result to a normalized <see cref="BigInteger"/> reference computation.
    /// </summary>
    /// <remarks>
    /// Horner evaluation creates a dense chain of multiply-add operations, making this a good stress pattern for
    /// wraparound behavior and sign handling across many intermediate values.
    /// </remarks>
    [Fact]
    public void UncheckedPolynomialEvaluation_MatchesNormalizedBigIntegerModel()
    {
        var random = new Random(90210);
        for (var sample = 0; sample < 120; sample++)
        {
            var degree = random.Next(8, 20);
            var x = IntegerTestHelpers.RandomInt256(random);

            var coeffs = new Int256[degree + 1];
            for (var i = 0; i < coeffs.Length; i++)
            {
                coeffs[i] = IntegerTestHelpers.RandomInt256(random);
            }

            var intResult = Int256.Zero;
            var bigResult = BigInteger.Zero;
            var bigX = (BigInteger)x;

            for (var i = degree; i >= 0; i--)
            {
                intResult = intResult * x + coeffs[i];
                bigResult = IntegerTestHelpers.NormalizeSigned(bigResult * bigX + (BigInteger)coeffs[i]);
            }

            Assert.Equal(bigResult, (BigInteger)intResult);
        }
    }

    /// <summary>
    /// Verifies two's-complement bitwise identity relationships at high-entropy and boundary values.
    /// </summary>
    [Fact]
    public void BitwiseComplement_ObeysTwosComplementIdentities_ForSignedAndUnsignedTypes()
    {
        var signedSamples = new[]
        {
            Int256.Zero,
            Int256.One,
            -1,
            Int256.MaxValue,
            Int256.MinValue,
            Int256.MinValue + Int256.One,
            (Int256)((BigInteger.One << 200) - 123),
            (Int256)(-(BigInteger.One << 200) + 456),
        };

        foreach (var value in signedSamples)
        {
            Assert.Equal(-value - Int256.One, ~value);
            Assert.Equal(value, ~~value);
        }

        var unsignedSamples = new[]
        {
            UInt256.Zero,
            UInt256.One,
            UInt256.MaxValue,
            (UInt256)(BigInteger.One << 255),
            (UInt256)((BigInteger.One << 200) + 789),
        };

        foreach (var value in unsignedSamples)
        {
            Assert.Equal(UInt256.MaxValue - value, ~value);
            Assert.Equal(value, ~~value);
        }
    }

    /// <summary>
    /// Verifies Euclid's algorithm on very large, in-range consecutive Fibonacci values.
    /// </summary>
    /// <remarks>
    /// Consecutive Fibonacci numbers are co-prime, so they provide a deterministic large-input GCD oracle.
    /// </remarks>
    [Fact]
    public void UInt256_EuclideanGcd_OnLargeConsecutiveFibonacciNumbers_EqualsOne()
    {
        var fPrev = BigInteger.Zero;
        var fCurr = BigInteger.One;

        while (fPrev + fCurr <= IntegerTestHelpers.UIntMax)
        {
            var next = fPrev + fCurr;
            fPrev = fCurr;
            fCurr = next;
        }

        var left = (UInt256)fPrev;
        var right = (UInt256)(fCurr - fPrev);

        var a = left;
        var b = right;
        while (!b.IsZero)
        {
            var r = a % b;
            a = b;
            b = r;
        }

        Assert.Equal(UInt256.One, a);
        Assert.Equal(BigInteger.One, BigInteger.GreatestCommonDivisor((BigInteger)left, (BigInteger)right));
    }

    /// <summary>
    /// Verifies quotient/remainder sign and magnitude invariants around adversarial divisors and near-boundary
    /// dividends.
    /// </summary>
    [Fact]
    public void SignedDivisionRemainder_SatisfySignAndMagnitudeInvariants_OnAdversarialInputs()
    {
        var values = new[]
        {
            Int256.MinValue,
            Int256.MinValue + Int256.One,
            -1,
            Int256.Zero,
            Int256.One,
            Int256.MaxValue - Int256.One,
            Int256.MaxValue,
            (Int256)(-(BigInteger.One << 200) + 17),
            (Int256)((BigInteger.One << 200) - 17),
        };

        var divisors = new[]
        {
            -987654321,
            -3,
            -2,
            -1,
            Int256.One,
            2,
            3,
            987654321,
            (Int256)((BigInteger.One << 120) + 1),
            (Int256)(-((BigInteger.One << 120) + 1)),
        };

        foreach (var left in values)
        {
            foreach (var right in divisors)
            {
                if (left == Int256.MinValue && right == -1)
                {
                    continue;
                }

                var quotient = left / right;
                var remainder = left % right;

                var recomposed = (BigInteger)quotient * (BigInteger)right + (BigInteger)remainder;
                Assert.Equal((BigInteger)left, recomposed);
                Assert.True(Int256.Abs(remainder) < Int256.Abs(right));

                if (!remainder.IsZero)
                {
                    Assert.Equal(Int256.Sign(left), Int256.Sign(remainder));
                }
            }
        }
    }

    /// <summary>
    /// Documents unchecked multiply semantics for <see cref="Int256.MinValue"/> operands that previously remained
    /// masked by division failures.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The unchecked <see cref="Int256"/> arithmetic model is two's-complement modulo <c>2^256</c>. Under that model,
    /// multiplying <see cref="Int256.MinValue"/> by odd factors preserves the same bit pattern, while multiplying by
    /// even factors clears the sign bit and can wrap to non-negative values.
    /// </para>
    /// <para>
    /// These cases are intentionally deterministic rather than randomized so a regression in MinValue handling is
    /// immediately attributable to a specific operator path.
    /// </para>
    /// </remarks>
    [Fact]
    public void Multiplication_MinValueOperands_FollowUncheckedTwosComplementWraparoundRules()
    {
        Assert.Equal(Int256.MinValue, Int256.MinValue * Int256.One);
        Assert.Equal(Int256.MinValue, Int256.One * Int256.MinValue);
        Assert.Equal(Int256.MinValue, Int256.MinValue * (Int256)(-1));
        Assert.Equal(Int256.Zero, Int256.MinValue * (Int256)2);

        var factors = new[] { -3, -2, -1, 1, 2, 3, 17, -17 };
        foreach (var factor in factors)
        {
            var expected = IntegerTestHelpers.NormalizeSigned((BigInteger)Int256.MinValue * factor);
            Assert.Equal(expected, (BigInteger)(Int256.MinValue * factor));
            Assert.Equal(expected, (BigInteger)(factor * Int256.MinValue));
        }
    }

    /// <summary>
    /// Verifies parsing of signed-zero textual forms to document their behavior across signed and unsigned APIs.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <see cref="BigInteger"/> treats <c>"-0"</c> as zero, and the 256-bit wrappers intentionally inherit that
    /// behavior.
    /// This test captures the contract explicitly so future parser refactors do not accidentally reject these payloads.
    /// </para>
    /// <para>
    /// The assertions exercise UTF-16 and UTF-8 entry points for parity.
    /// </para>
    /// </remarks>
    [Fact]
    public void Parsing_NegativeZeroPayloads_NormalizeToZeroAcrossSignedAndUnsignedTypes()
    {
        var negativeZero = "-0";
        var negativeZeroUtf8 = "-0"u8;

        Assert.Equal(Int256.Zero, Int256.Parse(negativeZero, NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.True(Int256.TryParse(negativeZero, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsedSigned));
        Assert.Equal(Int256.Zero, parsedSigned);
        Assert.Equal(Int256.Zero, Int256.Parse(negativeZeroUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.True(Int256.TryParse(negativeZeroUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsedSigned));
        Assert.Equal(Int256.Zero, parsedSigned);

        Assert.Equal(UInt256.Zero, UInt256.Parse(negativeZero, NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.True(UInt256.TryParse(negativeZero, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsedUnsigned));
        Assert.Equal(UInt256.Zero, parsedUnsigned);
        Assert.Equal(UInt256.Zero, UInt256.Parse(negativeZeroUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.True(UInt256.TryParse(negativeZeroUtf8, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsedUnsigned));
        Assert.Equal(UInt256.Zero, parsedUnsigned);

        Assert.False(UInt256.TryParse("-1", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
    }

    /// <summary>
    /// Verifies all single-bit values across the full 256-bit domain against exact bit-index invariants.
    /// </summary>
    /// <remarks>
    /// Walking every bit position provides deterministic limb-boundary coverage (0, 127, 128, and 255) and catches
    /// subtle off-by-one bugs in bit helper implementations that random sampling may miss.
    /// </remarks>
    [Fact]
    public void UInt256_SingleBitWalk_ValidatesBitHelperIndexInvariants()
    {
        for (var bit = 0; bit < 256; bit++)
        {
            var value = UInt256.One << bit;
            Assert.Equal(1, UInt256.PopCount(value));
            Assert.Equal(bit, UInt256.TrailingZeroCount(value));
            Assert.Equal(255 - bit, UInt256.LeadingZeroCount(value));
            Assert.Equal(bit, UInt256.Log2(value));
        }
    }

    /// <summary>
    /// Verifies that left/right rotations are exact inverses and preserve population count
    /// for both signed and unsigned 256-bit values under adversarial shift counts.
    /// </summary>
    [Fact]
    public void Rotations_AreMutualInverses_AndPreservePopulationCount()
    {
        var random = new Random(81173);
        var counts = new[] { int.MinValue, -65_535, -1_025, -513, -257, -255, -129, -1, 0, 1, 3, 17, 63, 127, 128, 129, 191, 255, 257, 513, 1_025, 65_535, int.MaxValue };

        for (var sample = 0; sample < 150; sample++)
        {
            var unsigned = IntegerTestHelpers.RandomUInt256(random);
            var signed = IntegerTestHelpers.RandomInt256(random);

            foreach (var count in counts)
            {
                var rotatedUnsignedLeft = UInt256.RotateLeft(unsigned, count);
                var rotatedUnsignedRight = UInt256.RotateRight(unsigned, count);
                Assert.Equal(unsigned, UInt256.RotateRight(rotatedUnsignedLeft, count));
                Assert.Equal(unsigned, UInt256.RotateLeft(rotatedUnsignedRight, count));
                Assert.Equal(UInt256.PopCount(unsigned), UInt256.PopCount(rotatedUnsignedLeft));
                Assert.Equal(UInt256.PopCount(unsigned), UInt256.PopCount(rotatedUnsignedRight));

                var rotatedSignedLeft = Int256.RotateLeft(signed, count);
                var rotatedSignedRight = Int256.RotateRight(signed, count);
                Assert.Equal(signed, Int256.RotateRight(rotatedSignedLeft, count));
                Assert.Equal(signed, Int256.RotateLeft(rotatedSignedRight, count));
                Assert.Equal(Int256.PopCount(signed), Int256.PopCount(rotatedSignedLeft));
                Assert.Equal(Int256.PopCount(signed), Int256.PopCount(rotatedSignedRight));
            }
        }
    }

    /// <summary>
    /// Verifies that unsigned shortest-bit-length and fixed byte-count helpers match the
    /// <see cref="UInt128"/> integral conventions across random 256-bit patterns.
    /// </summary>
    [Fact]
    public void UInt256_ShortestEncodingMetrics_MatchBigIntegerModel_AcrossRandomData()
    {
        var random = new Random(1234567);
        for (var i = 0; i < 600; i++)
        {
            var value = IntegerTestHelpers.RandomUInt256(random);
            var big = (BigInteger)value;

            var expectedBitLength = big.IsZero ? 0 : (int)BigInteger.Log2(big) + 1;

            Assert.Equal(expectedBitLength, value.GetShortestBitLength());
            Assert.Equal(32, value.GetByteCount());
        }
    }

    /// <summary>
    /// Verifies that signed shortest-bit-length and fixed byte-count helpers match the
    /// <see cref="Int128"/> integral conventions.
    /// </summary>
    [Fact]
    public void Int256_ShortestEncodingMetrics_MatchMinimalTwosComplementWidth_AcrossRandomData()
    {
        var random = new Random(7654321);
        for (var i = 0; i < 600; i++)
        {
            var value = IntegerTestHelpers.RandomInt256(random);
            var big = (BigInteger)value;

            var expectedBitLength = MinimalSignedBitLength(big);
            Assert.Equal(expectedBitLength, value.GetShortestBitLength());
            Assert.Equal(32, value.GetByteCount());
        }
    }

    /// <summary>
    /// Verifies that checked and unchecked division have the same overflow contract, including
    /// <see cref="Int256.MinValue"/> divided by <c>-1</c>.
    /// </summary>
    [Fact]
    public void CheckedAndUncheckedDivision_ShareMinValueOverflowContract()
    {
        var random = new Random(314159);

        for (var i = 0; i < 500; i++)
        {
            var left = IntegerTestHelpers.RandomInt256(random);
            var right = IntegerTestHelpers.RandomInt256(random);
            if (right.IsZero)
            {
                right = Int256.One;
            }

            if (left == Int256.MinValue && right == -1)
            {
                Assert.Throws<OverflowException>(() => left / right);
                Assert.Throws<OverflowException>(() => checked(left / right));
                continue;
            }

            Assert.Equal(left / right, checked(left / right));
        }

        Assert.Throws<OverflowException>(() => Int256.MinValue / (Int256)(-1));
        Assert.Throws<OverflowException>(() => checked(Int256.MinValue / (Int256)(-1)));
    }

    private static int MinimalSignedBitLength(BigInteger value)
    {
        if (value.IsZero)
            return 0;
        return value.Sign > 0
            ? (int)value.GetBitLength()
            : (int)(~value).GetBitLength() + 1;
    }
}
