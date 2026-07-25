using System.Numerics;
using Xunit;

namespace Durable7.Numerics.Tests;

/// <summary>
/// Exercises the sparse non-negative integer representation imported from the A002845 workspace.
/// </summary>
public sealed class SparseIntegerTests
{
    /// <summary>Verifies small values compare, format, and round-trip through supported conversion APIs.</summary>
    [Fact]
    public void SmallValues_CompareFormatAndConvert()
    {
        SparseInteger zero = 0UL;
        SparseInteger one = 1UL;
        SparseInteger two = 2UL;
        SparseInteger three = one + two;
        SparseInteger fortyTwo = 42UL;

        Assert.True(1UL == one);
        Assert.True(zero == 0UL);
        Assert.True(one != 0UL);
        Assert.True(one != 2UL);
        Assert.True(one < 2UL);
        Assert.True(one > zero);
        Assert.True(zero < fortyTwo);
        Assert.True(three == 3UL);
        Assert.True(three > 2UL);
        Assert.False(three < 3UL);
        Assert.True(one.Equals((object)(SparseInteger)1UL));
        Assert.False(one.Equals((object)3UL));
        Assert.False(one.Equals(null));
        Assert.True(one.CompareTo(2UL) < 0);
        Assert.True(three.CompareTo(two) > 0);
        Assert.Equal("42", fortyTwo.ToString());
        Assert.Equal("42", (string)fortyTwo);
        Assert.Equal(fortyTwo, SparseInteger.Parse("42"));
        Assert.Equal(fortyTwo, (SparseInteger)"42");
        Assert.Equal(new BigInteger(42), (BigInteger)fortyTwo);
        Assert.Equal(fortyTwo, (SparseInteger)new BigInteger(42));
    }

    /// <summary>Verifies sparse powers and arithmetic preserve values that exceed built-in integer widths.</summary>
    [Fact]
    public void SparsePowers_AddAndMultiplyBeyondUlong()
    {
        SparseInteger exponent = 130UL;
        SparseInteger power = exponent.Exp2();
        SparseInteger sum = power + 5UL;
        SparseInteger product = power * 3UL;

        BigInteger expectedPower = BigInteger.One << 130;

        Assert.Equal(expectedPower, (BigInteger)power);
        Assert.Equal(expectedPower + 5, (BigInteger)sum);
        Assert.Equal(expectedPower * 3, (BigInteger)product);
        Assert.Equal(exponent, power.Log2());
    }

    /// <summary>Verifies power computation composes logarithms and exponents for exact powers of two.</summary>
    [Fact]
    public void Power_ComposesSparseExponents()
    {
        SparseInteger four = 4UL;
        SparseInteger exponent = 65UL;

        SparseInteger result = four.Power(exponent);

        Assert.Equal(BigInteger.One << 130, (BigInteger)result);
    }

    /// <summary>Verifies comparison remains stable for large values imported through <see cref="BigInteger"/>.</summary>
    [Fact]
    public void LargeBigIntegerConversions_CompareAgainstSmallValues()
    {
        SparseInteger two = 2UL;
        BigInteger big = (BigInteger)decimal.MaxValue;
        SparseInteger huge = (SparseInteger)(big * big);

        Assert.True(huge.Equals(huge));
        Assert.True(huge.Equals((object)huge));
        Assert.Equal(0, huge.CompareTo(huge));
        Assert.True(two.CompareTo(huge) < 0);
        Assert.True(huge.CompareTo(two) > 0);
    }

    /// <summary>Verifies invalid logarithm and negative conversion inputs are rejected.</summary>
    [Fact]
    public void InvalidInputs_Throw()
    {
        Assert.Throws<InvalidOperationException>(() => ((SparseInteger)3UL).Log2());
        Assert.Throws<OverflowException>(() => (SparseInteger)new BigInteger(-1));
    }

    /// <summary>
    /// Verifies carry cascades where a dense low value meets scattered high bits. This shape used to
    /// lose the accumulated partial sum when an intermediate carry sum fit into <see cref="ulong"/>.
    /// </summary>
    [Fact]
    public void Addition_CarryAcrossSmallBoundary_MatchesBigInteger()
    {
        BigInteger twoTo64 = BigInteger.One << 64;

        SparseInteger left = 7UL;
        SparseInteger right = (SparseInteger)(twoTo64 + 1);
        Assert.Equal(twoTo64 + 8, (BigInteger)(left + right));

        Assert.Equal(
            (twoTo64 << 1) + 16,
            (BigInteger)((SparseInteger)13UL + (SparseInteger)((twoTo64 << 1) + 3)));
    }

    /// <summary>
    /// Verifies addition, multiplication, and comparison against a <see cref="BigInteger"/> model over
    /// randomized operands biased toward carry-heavy shapes (dense low bits plus scattered high bits).
    /// </summary>
    [Fact]
    public void RandomizedArithmetic_MatchesBigIntegerModel()
    {
        var random = new Random(20260709);

        for (int iteration = 0; iteration < 2_000; iteration++)
        {
            BigInteger leftModel = NextOperand(random);
            BigInteger rightModel = NextOperand(random);
            SparseInteger left = (SparseInteger)leftModel;
            SparseInteger right = (SparseInteger)rightModel;

            Assert.Equal(leftModel + rightModel, (BigInteger)(left + right));
            Assert.Equal(leftModel.CompareTo(rightModel), Math.Sign(left.CompareTo(right)));

            // Keep products convertible to BigInteger: multiply a full operand by a small sparse one.
            BigInteger sparseModel = (BigInteger.One << random.Next(0, 130)) + (ulong)random.Next(0, 4);
            Assert.Equal(leftModel * sparseModel, (BigInteger)(left * (SparseInteger)sparseModel));
        }
    }

    private static BigInteger NextOperand(Random random)
    {
        // Dense low word.
        BigInteger result = (ulong)random.NextInt64();
        if (random.Next(2) == 0)
        {
            result = (ulong)random.Next(0, 64);
        }

        // A few scattered high bits, straddling the 2^64 small-value boundary.
        int highBits = random.Next(0, 4);
        for (int i = 0; i < highBits; i++)
        {
            result += BigInteger.One << random.Next(60, 200);
        }

        return result;
    }
}
