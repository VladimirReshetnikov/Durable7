using System.Globalization;
using System.Numerics;
using Xunit;

namespace Durable7.Numerics.Tests;

/// <summary>Regression coverage for shared limb division and span formatting.</summary>
public sealed class WideIntegerAlgorithmTests
{
    /// <summary>Checks normalized multi-limb quotient and remainder against deterministic random models.</summary>
    [Fact]
    public void RandomizedLimbDivision_MatchesBigIntegerAcrossWidths()
    {
        var random = new Random(20260710);
        for (int iteration = 0; iteration < 1_000; iteration++)
        {
            UInt256 left256 = IntegerTestHelpers.RandomUInt256(random);
            UInt256 right256 = IntegerTestHelpers.RandomUInt256(random) | UInt256.One;
            Assert.Equal((BigInteger)left256 / (BigInteger)right256, (BigInteger)(left256 / right256));
            Assert.Equal((BigInteger)left256 % (BigInteger)right256, (BigInteger)(left256 % right256));

            UInt512 left512 = IntegerTestHelpers.RandomUInt512(random);
            UInt512 right512 = IntegerTestHelpers.RandomUInt512(random) | UInt512.One;
            Assert.Equal((BigInteger)left512 / (BigInteger)right512, (BigInteger)(left512 / right512));
            Assert.Equal((BigInteger)left512 % (BigInteger)right512, (BigInteger)(left512 % right512));

            UInt1024 left1024 = IntegerTestHelpers.RandomUInt1024(random);
            UInt1024 right1024 = IntegerTestHelpers.RandomUInt1024(random) | UInt1024.One;
            Assert.Equal((BigInteger)left1024 / (BigInteger)right1024, (BigInteger)(left1024 / right1024));
            Assert.Equal((BigInteger)left1024 % (BigInteger)right1024, (BigInteger)(left1024 % right1024));
        }
    }

    /// <summary>Checks that the common decimal character and UTF-8 span paths allocate no managed objects.</summary>
    [Fact]
    public void DefaultTryFormat_WritesWithoutManagedAllocations()
    {
        UInt1024 value = UInt1024.MaxValue;
        Int1024 signedValue = Int1024.MinValue;
        Span<char> characters = stackalloc char[512];
        Span<byte> bytes = stackalloc byte[512];

        for (int i = 0; i < 32; i++)
        {
            Assert.True(value.TryFormat(characters, out _, default, CultureInfo.InvariantCulture));
            Assert.True(value.TryFormat(bytes, out _, default, CultureInfo.InvariantCulture));
            Assert.True(value.TryFormat(characters, out _, "D320", CultureInfo.InvariantCulture));
            Assert.True(value.TryFormat(characters, out _, "N0", CultureInfo.InvariantCulture));
            Assert.True(value.TryFormat(bytes, out _, "x256", CultureInfo.InvariantCulture));
            Assert.True(signedValue.TryFormat(characters, out _, "D320", CultureInfo.InvariantCulture));
            Assert.True(signedValue.TryFormat(characters, out _, "N0", CultureInfo.InvariantCulture));
            Assert.True(signedValue.TryFormat(bytes, out _, "X256", CultureInfo.InvariantCulture));
        }

        long beforeUnsigned = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 1_000; i++)
        {
            if (!value.TryFormat(characters, out int charsWritten, default, CultureInfo.InvariantCulture) ||
                charsWritten != 309 ||
                !value.TryFormat(bytes, out int bytesWritten, default, CultureInfo.InvariantCulture) ||
                bytesWritten != 309 ||
                !value.TryFormat(characters, out _, "D320", CultureInfo.InvariantCulture) ||
                !value.TryFormat(characters, out _, "N0", CultureInfo.InvariantCulture) ||
                !value.TryFormat(bytes, out _, "x256", CultureInfo.InvariantCulture))
            {
                throw new Xunit.Sdk.XunitException("Unexpected span-format result.");
            }
        }
        long unsignedAllocated = GC.GetAllocatedBytesForCurrentThread() - beforeUnsigned;

        long beforeSignedDecimal = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 1_000; i++)
            if (!signedValue.TryFormat(characters, out _, "D320", CultureInfo.InvariantCulture))
                throw new Xunit.Sdk.XunitException("Unexpected signed decimal span-format result.");
        long signedDecimalAllocated = GC.GetAllocatedBytesForCurrentThread() - beforeSignedDecimal;

        long beforeSignedNumber = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 1_000; i++)
            if (!signedValue.TryFormat(characters, out _, "N0", CultureInfo.InvariantCulture))
                throw new Xunit.Sdk.XunitException("Unexpected signed number span-format result.");
        long signedNumberAllocated = GC.GetAllocatedBytesForCurrentThread() - beforeSignedNumber;

        long beforeSignedHex = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 1_000; i++)
            if (!signedValue.TryFormat(bytes, out _, "X256", CultureInfo.InvariantCulture))
                throw new Xunit.Sdk.XunitException("Unexpected signed hexadecimal span-format result.");
        long signedHexAllocated = GC.GetAllocatedBytesForCurrentThread() - beforeSignedHex;

        Assert.True(
            unsignedAllocated == 0 && signedDecimalAllocated == 0 && signedNumberAllocated == 0 && signedHexAllocated == 0,
            $"Allocated bytes: unsigned={unsignedAllocated}, signed D={signedDecimalAllocated}, signed N={signedNumberAllocated}, signed X={signedHexAllocated}.");
    }

    /// <summary>Checks that formatting observes mutations to caller-owned number-format metadata.</summary>
    [Fact]
    public void NumberFormat_MutableGroupingIsNotCached()
    {
        var format = (NumberFormatInfo)CultureInfo.InvariantCulture.NumberFormat.Clone();
        Span<char> destination = stackalloc char[32];
        UInt256 value = 12_345_678;

        format.NumberGroupSizes = [2];
        Assert.True(value.TryFormat(destination, out int written, "N0", format));
        Assert.Equal("12,34,56,78", destination[..written].ToString());

        format.NumberGroupSizes = [4];
        Assert.True(value.TryFormat(destination, out written, "N0", format));
        Assert.Equal("1234,5678", destination[..written].ToString());
    }

    /// <summary>Checks a long carry cascade across two dense sparse bit-position streams.</summary>
    [Fact]
    public void SparseAddition_MergesDenseBitRuns()
    {
        BigInteger dense = (BigInteger.One << 8_192) - 1;
        SparseInteger left = (SparseInteger)dense;
        SparseInteger right = (SparseInteger)dense;

        Assert.Equal(dense + dense, (BigInteger)(left + right));
    }
}
