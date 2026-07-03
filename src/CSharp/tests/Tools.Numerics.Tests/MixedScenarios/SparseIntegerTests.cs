using System.Numerics;
using Xunit;

namespace Tools.Numerics.Tests;

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
}
