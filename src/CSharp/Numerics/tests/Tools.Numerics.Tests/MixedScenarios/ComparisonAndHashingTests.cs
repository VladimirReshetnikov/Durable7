using System.Numerics;
using Xunit;

namespace Tools.Numerics.Tests;

/// <summary>
/// Validates ordering, equality, and hash-code contracts that intentionally cross type quadrants.
/// </summary>
/// <remarks>
/// <para>
/// The project keeps arithmetic and parsing tests in width/signedness-specific suites. This suite is different: it
/// captures comparison semantics that are easiest to audit side-by-side across all four integer quadrants
/// (UInt256/Int256/UInt512/Int512).
/// </para>
/// <para>
/// Method ordering is deliberate and stable:
/// </para>
/// <list type="number">
/// <item><description>Unsigned 256-bit counterpart.</description></item>
/// <item><description>Signed 256-bit counterpart.</description></item>
/// <item><description>Unsigned 512-bit counterpart.</description></item>
/// <item><description>Signed 512-bit counterpart.</description></item>
/// <item><description>Explicitly mixed scenarios that intentionally compare behaviors across quadrants.</description></item>
/// </list>
/// </remarks>
public sealed class ComparisonAndHashingTests
{
    /// <summary>
    /// Verifies the <see cref="UInt256"/> ordering, equality, and hash-code contracts remain mutually consistent.
    /// </summary>
    /// <remarks>
    /// This test guards the relationship between <c>Sort</c>, relational operators, and
    /// <see cref="UInt256.CompareTo(UInt256)"/> by including both tiny values and very large sparse values.
    /// It also confirms identical values produce identical hash codes while distinct values remain non-equal.
    /// </remarks>
    [Fact]
    public void UInt256_ComparisonEqualityAndHashing_AreConsistent()
    {
        var values = new List<UInt256> { UInt256.Zero, UInt256.One, UInt256.MaxValue, (UInt256)(BigInteger.One << 200), (UInt256)((BigInteger.One << 200) + 1) };
        values.Sort();

        for (var i = 1; i < values.Count; i++)
        {
            Assert.True(values[i - 1] <= values[i]);
            Assert.True(values[i].CompareTo(values[i - 1]) >= 0);
        }

        var a = (UInt256)123456u;
        var b = (UInt256)123456u;
        var c = (UInt256)123457u;
        Assert.True(a == b);
        Assert.Equal(a.GetHashCode(), b.GetHashCode());
        Assert.True(a != c);
    }

    /// <summary>
    /// Verifies signed 256-bit comparison, equality, and hash-code semantics stay aligned across negative and
    /// positive ranges.
    /// </summary>
    /// <remarks>
    /// The fixture intentionally spans <see cref="Int256.MinValue"/>, <see cref="Int256.MaxValue"/>, and
    /// high-magnitude positive/negative values to ensure sorting and <see cref="Int256.CompareTo(Int256)"/>
    /// ordering remain coherent when sign bits differ.
    /// </remarks>
    [Fact]
    public void Int256_ComparisonEqualityAndHashing_AreConsistent()
    {
        var values = new List<Int256> { Int256.Zero, Int256.One, -1, Int256.MinValue, Int256.MaxValue, (Int256)(BigInteger.One << 200), (Int256)(-(BigInteger.One << 200)) };
        values.Sort();

        for (var i = 1; i < values.Count; i++)
        {
            Assert.True(values[i - 1] <= values[i]);
            Assert.True(values[i].CompareTo(values[i - 1]) >= 0);
        }

        var a = (Int256)(-77);
        var b = (Int256)(-77);
        var c = (Int256)(-76);
        Assert.True(a == b);
        Assert.Equal(a.GetHashCode(), b.GetHashCode());
        Assert.True(a != c);
    }

    /// <summary>
    /// Verifies the <see cref="UInt512"/> comparison and hashing contracts using representative low, high, and
    /// adjacent values.
    /// </summary>
    /// <remarks>
    /// Adjacent high-magnitude values are included to detect ordering or carry-related regressions that might only
    /// appear near the upper width boundary.
    /// </remarks>
    [Fact]
    public void UInt512_ComparisonEqualityAndHashing_AreConsistent()
    {
        var values = new List<UInt512> { UInt512.Zero, UInt512.One, UInt512.MaxValue, (UInt512)(BigInteger.One << 420), (UInt512)((BigInteger.One << 420) + 1) };
        values.Sort();

        for (var i = 1; i < values.Count; i++)
        {
            Assert.True(values[i - 1] <= values[i]);
            Assert.True(values[i].CompareTo(values[i - 1]) >= 0);
        }

        var a = (UInt512)123456u;
        var b = (UInt512)123456u;
        var c = (UInt512)123457u;
        Assert.True(a == b);
        Assert.Equal(a.GetHashCode(), b.GetHashCode());
        Assert.True(a != c);
    }

    /// <summary>
    /// Verifies signed 512-bit comparison and hashing contracts remain internally consistent across extreme values.
    /// </summary>
    /// <remarks>
    /// This is the signed counterpart to the unsigned 512-bit test and confirms that
    /// <see cref="Int512.CompareTo(Int512)"/>, relational operators, and equality/hash behavior agree even when
    /// sign-extreme values are part of the sorted corpus.
    /// </remarks>
    [Fact]
    public void Int512_ComparisonEqualityAndHashing_AreConsistent()
    {
        var values = new List<Int512> { Int512.Zero, Int512.One, -1, Int512.MinValue, Int512.MaxValue, (Int512)(BigInteger.One << 420), (Int512)(-(BigInteger.One << 420)) };
        values.Sort();

        for (var i = 1; i < values.Count; i++)
        {
            Assert.True(values[i - 1] <= values[i]);
            Assert.True(values[i].CompareTo(values[i - 1]) >= 0);
        }

        var a = (Int512)(-77);
        var b = (Int512)(-77);
        var c = (Int512)(-76);
        Assert.True(a == b);
        Assert.Equal(a.GetHashCode(), b.GetHashCode());
        Assert.True(a != c);
    }

    /// <summary>
    /// Verifies the non-generic <see cref="IComparable.CompareTo(object?)"/> contract across all four integer
    /// quadrants.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Per the BCL contract, comparing against <see langword="null"/> must return a value greater than zero.
    /// </para>
    /// <para>
    /// Comparing against an object of an incompatible runtime type must throw <see cref="ArgumentException"/>.
    /// </para>
    /// </remarks>
    [Fact]
    public void IComparable_ObjectCompareTo_EnforcesTypeContracts_AcrossQuadrants()
    {
        IComparable[] values = [(Int256)7, (UInt256)7, (Int512)7, (UInt512)7];

        foreach (var value in values)
        {
            Assert.True(value.CompareTo(null) > 0);
            Assert.Throws<ArgumentException>(() => value.CompareTo("7"));
        }
    }
}
