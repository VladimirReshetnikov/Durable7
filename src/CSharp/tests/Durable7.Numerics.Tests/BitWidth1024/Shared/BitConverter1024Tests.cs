using System.Numerics;
using Xunit;

namespace Durable7.Numerics.Tests;

/// <summary>
/// Validates the 1024-bit <see cref="BitConverterEx"/> overloads for behavior parity with core
/// <see cref="BitConverter"/> conventions.
/// </summary>
public sealed class BitConverterEx1024Tests
{
    /// <summary>
    /// Verifies that <see cref="BitConverterEx.IsLittleEndian"/> mirrors the runtime architecture endianness flag.
    /// </summary>
    [Fact]
    public void IsLittleEndian_MatchesRuntimeBitConverter()
    {
        Assert.Equal(BitConverter.IsLittleEndian, BitConverterEx.IsLittleEndian);
    }

    /// <summary>
    /// Verifies that unsigned values are serialized using machine endianness and can be parsed back losslessly.
    /// </summary>
    [Fact]
    public void GetBytes_UInt1024_UsesMachineEndiannessAndRoundTrips()
    {
        var values = new[]
        {
            UInt1024.Zero,
            UInt1024.One,
            UInt1024.MaxValue,
            (UInt1024)(BigInteger.One << 200),
            IntegerTestHelpers.RandomUInt1024(new Random(1001))
        };

        foreach (var value in values)
        {
            var bytes = BitConverterEx.GetBytes(value);
            Assert.Equal(128, bytes.Length);
            Assert.Equal(ExpectedBytes(value), bytes);
            Assert.Equal(value, BitConverterEx.ToUInt1024(bytes, 0));
            Assert.Equal(value, BitConverterEx.ToUInt1024(bytes));
        }
    }

    /// <summary>
    /// Verifies that signed values are serialized using machine endianness and can be parsed back losslessly.
    /// </summary>
    [Fact]
    public void GetBytes_Int1024_UsesMachineEndiannessAndRoundTrips()
    {
        var values = new[]
        {
            Int1024.Zero,
            Int1024.One,
            Int1024.MinValue,
            Int1024.MaxValue,
            (Int1024)(-(BigInteger.One << 200)),
            IntegerTestHelpers.RandomInt1024(new Random(2024))
        };

        foreach (var value in values)
        {
            var bytes = BitConverterEx.GetBytes(value);
            Assert.Equal(128, bytes.Length);
            Assert.Equal(ExpectedBytes(value), bytes);
            Assert.Equal(value, BitConverterEx.ToInt1024(bytes, 0));
            Assert.Equal(value, BitConverterEx.ToInt1024(bytes));
        }
    }

    /// <summary>
    /// Verifies span-writing behavior for both insufficient and sufficient destinations.
    /// </summary>
    [Fact]
    public void TryWriteBytes_WritesAtBeginningAndReturnsLengthContract()
    {
        Span<byte> tooSmall = stackalloc byte[127];
        Assert.False(BitConverterEx.TryWriteBytes(tooSmall, UInt1024.One));
        Assert.False(BitConverterEx.TryWriteBytes(tooSmall, Int1024.One));

        byte[] target = new byte[160];
        target.AsSpan().Fill(0xCC);

        var uintValue = (UInt1024)(BigInteger.One << 180) + 12345;
        var intValue = (Int1024)(-(BigInteger.One << 180)) + 6789;

        Assert.True(BitConverterEx.TryWriteBytes(target.AsSpan(), uintValue));
        Assert.Equal(ExpectedBytes(uintValue), target[..128]);
        Assert.All(target[128..], b => Assert.Equal(0xCC, b));

        target.AsSpan().Fill(0xCC);
        Assert.True(BitConverterEx.TryWriteBytes(target.AsSpan(), intValue));
        Assert.Equal(ExpectedBytes(intValue), target[..128]);
        Assert.All(target[128..], b => Assert.Equal(0xCC, b));
    }

    /// <summary>
    /// Verifies that array overloads honor non-zero start indices when decoding 1024-bit payloads.
    /// </summary>
    [Fact]
    public void To1024_ArrayOverloads_RespectStartIndex()
    {
        var uintValue = IntegerTestHelpers.RandomUInt1024(new Random(10));
        var intValue = IntegerTestHelpers.RandomInt1024(new Random(11));

        var uintBytes = ExpectedBytes(uintValue);
        var intBytes = ExpectedBytes(intValue);

        byte[] uintContainer = new byte[140];
        new byte[] { 1, 2, 3, 4, 5 }.CopyTo(uintContainer, 0);
        uintBytes.CopyTo(uintContainer, 5);

        byte[] intContainer = new byte[200];
        intContainer[71] = 0xAA;
        intBytes.CopyTo(intContainer, 72);

        Assert.Equal(uintValue, BitConverterEx.ToUInt1024(uintContainer, 5));
        Assert.Equal(intValue, BitConverterEx.ToInt1024(intContainer, 72));
    }

    /// <summary>
    /// Verifies guard-rail exceptions for null arrays, invalid indices, and arrays that are too short for 128-byte
    /// reads.
    /// </summary>
    [Fact]
    public void To1024_ArrayOverloads_ThrowForInvalidInput()
    {
        Assert.Throws<ArgumentNullException>(() => BitConverterEx.ToUInt1024(null!, 0));
        Assert.Throws<ArgumentNullException>(() => BitConverterEx.ToInt1024(null!, 0));

        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToUInt1024(new byte[128], -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToInt1024(new byte[128], -1));

        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToUInt1024(new byte[128], 128));
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToInt1024(new byte[128], 128));

        Assert.Throws<ArgumentException>(() => BitConverterEx.ToUInt1024(new byte[128], 33));
        Assert.Throws<ArgumentException>(() => BitConverterEx.ToInt1024(new byte[128], 33));

        Assert.Throws<ArgumentException>(() => BitConverterEx.ToUInt1024(new byte[63], 0));
        Assert.Throws<ArgumentException>(() => BitConverterEx.ToInt1024(new byte[63], 0));
    }

    /// <summary>
    /// Verifies that span overloads reject inputs shorter than the fixed 128-byte binary width.
    /// </summary>
    [Fact]
    public void To1024_SpanOverloads_ThrowForShortInput()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToUInt1024(new byte[63]));
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToInt1024(new byte[63]));
    }

    /// <summary>
    /// Verifies span decoders read exactly the first 128 bytes and ignore trailing payload.
    /// </summary>
    [Fact]
    public void To1024_SpanOverloads_ReadFixedWidthPrefix()
    {
        var uintValue = (UInt1024)((BigInteger.One << 190) + 1281);
        var intValue = (Int1024)(-(BigInteger.One << 187) + 654);

        var uintBytes = ExpectedBytes(uintValue);
        var intBytes = ExpectedBytes(intValue);

        byte[] uintContainer = new byte[160];
        byte[] intContainer = new byte[160];
        uintBytes.CopyTo(uintContainer, 0);
        intBytes.CopyTo(intContainer, 0);
        Array.Fill(uintContainer, (byte)0xAA, 128, uintContainer.Length - 128);
        Array.Fill(intContainer, (byte)0x55, 128, intContainer.Length - 128);

        Assert.Equal(uintValue, BitConverterEx.ToUInt1024(uintContainer));
        Assert.Equal(intValue, BitConverterEx.ToInt1024(intContainer));
    }

    /// <summary>
    /// Verifies span decoding uses exactly the first 128 bytes and ignores any trailing payload bytes.
    /// </summary>
    [Fact]
    public void To1024_SpanOverloads_IgnoreTrailingBytesBeyondFirst128()
    {
        var unsignedValue = IntegerTestHelpers.RandomUInt1024(new Random(901));
        var signedValue = IntegerTestHelpers.RandomInt1024(new Random(902));

        byte[] unsignedWithTail = new byte[160];
        byte[] expectedUnsigned = ExpectedBytes(unsignedValue);
        expectedUnsigned.CopyTo(unsignedWithTail, 0);
        Array.Fill(unsignedWithTail, (byte)0xCC, 128, unsignedWithTail.Length - 128);

        byte[] signedWithTail = new byte[160];
        byte[] expectedSigned = ExpectedBytes(signedValue);
        expectedSigned.CopyTo(signedWithTail, 0);
        Array.Fill(signedWithTail, (byte)0xDD, 128, signedWithTail.Length - 128);

        Assert.Equal(unsignedValue, BitConverterEx.ToUInt1024(unsignedWithTail));
        Assert.Equal(signedValue, BitConverterEx.ToInt1024(signedWithTail));
    }

    private static byte[] ExpectedBytes(UInt1024 value)
    {
        var bytes = ((BigInteger)value).ToByteArray(isUnsigned: true, isBigEndian: false);
        return NormalizeToMachineEndianness(bytes);
    }

    private static byte[] ExpectedBytes(Int1024 value)
    {
        var source = ((BigInteger)value).ToByteArray(isUnsigned: false, isBigEndian: false);
        byte[] bytes = new byte[128];
        Array.Copy(source, 0, bytes, 0, Math.Min(source.Length, 128));

        if ((BigInteger)value < 0 && source.Length < 128)
            Array.Fill(bytes, (byte)0xFF, source.Length, 128 - source.Length);

        if (!BitConverter.IsLittleEndian)
            Array.Reverse(bytes);

        return bytes;
    }

    private static byte[] NormalizeToMachineEndianness(byte[] littleEndian)
    {
        byte[] bytes = new byte[128];
        Array.Copy(littleEndian, 0, bytes, 0, Math.Min(littleEndian.Length, 128));

        if (!BitConverter.IsLittleEndian)
            Array.Reverse(bytes);

        return bytes;
    }
}
