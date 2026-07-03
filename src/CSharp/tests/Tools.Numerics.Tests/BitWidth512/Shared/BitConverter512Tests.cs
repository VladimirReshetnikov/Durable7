using System.Numerics;
using Xunit;

namespace Tools.Numerics.Tests;

/// <summary>
/// Validates the 512-bit <see cref="BitConverterEx"/> overloads for behavior parity with core
/// <see cref="BitConverter"/> conventions.
/// </summary>
public sealed class BitConverterEx512Tests
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
    public void GetBytes_UInt512_UsesMachineEndiannessAndRoundTrips()
    {
        var values = new[]
        {
            UInt512.Zero,
            UInt512.One,
            UInt512.MaxValue,
            (UInt512)(BigInteger.One << 200),
            IntegerTestHelpers.RandomUInt512(new Random(1001))
        };

        foreach (var value in values)
        {
            var bytes = BitConverterEx.GetBytes(value);
            Assert.Equal(64, bytes.Length);
            Assert.Equal(ExpectedBytes(value), bytes);
            Assert.Equal(value, BitConverterEx.ToUInt512(bytes, 0));
            Assert.Equal(value, BitConverterEx.ToUInt512(bytes));
        }
    }

    /// <summary>
    /// Verifies that signed values are serialized using machine endianness and can be parsed back losslessly.
    /// </summary>
    [Fact]
    public void GetBytes_Int512_UsesMachineEndiannessAndRoundTrips()
    {
        var values = new[]
        {
            Int512.Zero,
            Int512.One,
            Int512.MinValue,
            Int512.MaxValue,
            (Int512)(-(BigInteger.One << 200)),
            IntegerTestHelpers.RandomInt512(new Random(2024))
        };

        foreach (var value in values)
        {
            var bytes = BitConverterEx.GetBytes(value);
            Assert.Equal(64, bytes.Length);
            Assert.Equal(ExpectedBytes(value), bytes);
            Assert.Equal(value, BitConverterEx.ToInt512(bytes, 0));
            Assert.Equal(value, BitConverterEx.ToInt512(bytes));
        }
    }

    /// <summary>
    /// Verifies span-writing behavior for both insufficient and sufficient destinations.
    /// </summary>
    [Fact]
    public void TryWriteBytes_WritesAtBeginningAndReturnsLengthContract()
    {
        Span<byte> tooSmall = stackalloc byte[63];
        Assert.False(BitConverterEx.TryWriteBytes(tooSmall, UInt512.One));
        Assert.False(BitConverterEx.TryWriteBytes(tooSmall, Int512.One));

        byte[] target = new byte[80];
        target.AsSpan().Fill(0xCC);

        var uintValue = (UInt512)(BigInteger.One << 180) + 12345;
        var intValue = (Int512)(-(BigInteger.One << 180)) + 6789;

        Assert.True(BitConverterEx.TryWriteBytes(target.AsSpan(), uintValue));
        Assert.Equal(ExpectedBytes(uintValue), target[..64]);
        Assert.All(target[64..], b => Assert.Equal(0xCC, b));

        target.AsSpan().Fill(0xCC);
        Assert.True(BitConverterEx.TryWriteBytes(target.AsSpan(), intValue));
        Assert.Equal(ExpectedBytes(intValue), target[..64]);
        Assert.All(target[64..], b => Assert.Equal(0xCC, b));
    }

    /// <summary>
    /// Verifies that array overloads honor non-zero start indices when decoding 512-bit payloads.
    /// </summary>
    [Fact]
    public void To512_ArrayOverloads_RespectStartIndex()
    {
        var uintValue = IntegerTestHelpers.RandomUInt512(new Random(10));
        var intValue = IntegerTestHelpers.RandomInt512(new Random(11));

        var uintBytes = ExpectedBytes(uintValue);
        var intBytes = ExpectedBytes(intValue);

        byte[] container = new byte[140];
        new byte[] { 1, 2, 3, 4, 5 }.CopyTo(container, 0);
        uintBytes.CopyTo(container, 5);
        intBytes.CopyTo(container, 72);

        Assert.Equal(uintValue, BitConverterEx.ToUInt512(container, 5));
        Assert.Equal(intValue, BitConverterEx.ToInt512(container, 72));
    }

    /// <summary>
    /// Verifies guard-rail exceptions for null arrays, invalid indices, and arrays that are too short for 64-byte
    /// reads.
    /// </summary>
    [Fact]
    public void To512_ArrayOverloads_ThrowForInvalidInput()
    {
        Assert.Throws<ArgumentNullException>(() => BitConverterEx.ToUInt512(null!, 0));
        Assert.Throws<ArgumentNullException>(() => BitConverterEx.ToInt512(null!, 0));

        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToUInt512(new byte[64], -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToInt512(new byte[64], -1));

        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToUInt512(new byte[64], 64));
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToInt512(new byte[64], 64));

        Assert.Throws<ArgumentException>(() => BitConverterEx.ToUInt512(new byte[64], 33));
        Assert.Throws<ArgumentException>(() => BitConverterEx.ToInt512(new byte[64], 33));

        Assert.Throws<ArgumentException>(() => BitConverterEx.ToUInt512(new byte[63], 0));
        Assert.Throws<ArgumentException>(() => BitConverterEx.ToInt512(new byte[63], 0));
    }

    /// <summary>
    /// Verifies that span overloads reject inputs shorter than the fixed 64-byte binary width.
    /// </summary>
    [Fact]
    public void To512_SpanOverloads_ThrowForShortInput()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToUInt512(new byte[63]));
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToInt512(new byte[63]));
    }

    /// <summary>
    /// Verifies span decoders read exactly the first 64 bytes and ignore trailing payload.
    /// </summary>
    [Fact]
    public void To512_SpanOverloads_ReadFixedWidthPrefix()
    {
        var uintValue = (UInt512)((BigInteger.One << 190) + 641);
        var intValue = (Int512)(-(BigInteger.One << 187) + 654);

        var uintBytes = ExpectedBytes(uintValue);
        var intBytes = ExpectedBytes(intValue);

        byte[] uintContainer = new byte[80];
        byte[] intContainer = new byte[96];
        uintBytes.CopyTo(uintContainer, 0);
        intBytes.CopyTo(intContainer, 0);
        Array.Fill(uintContainer, (byte)0xAA, 64, uintContainer.Length - 64);
        Array.Fill(intContainer, (byte)0x55, 64, intContainer.Length - 64);

        Assert.Equal(uintValue, BitConverterEx.ToUInt512(uintContainer));
        Assert.Equal(intValue, BitConverterEx.ToInt512(intContainer));
    }

    /// <summary>
    /// Verifies span decoding uses exactly the first 64 bytes and ignores any trailing payload bytes.
    /// </summary>
    [Fact]
    public void To512_SpanOverloads_IgnoreTrailingBytesBeyondFirst64()
    {
        var unsignedValue = IntegerTestHelpers.RandomUInt512(new Random(901));
        var signedValue = IntegerTestHelpers.RandomInt512(new Random(902));

        byte[] unsignedWithTail = new byte[128];
        byte[] expectedUnsigned = ExpectedBytes(unsignedValue);
        expectedUnsigned.CopyTo(unsignedWithTail, 0);
        Array.Fill(unsignedWithTail, (byte)0xCC, 64, 64);

        byte[] signedWithTail = new byte[128];
        byte[] expectedSigned = ExpectedBytes(signedValue);
        expectedSigned.CopyTo(signedWithTail, 0);
        Array.Fill(signedWithTail, (byte)0xDD, 64, 64);

        Assert.Equal(unsignedValue, BitConverterEx.ToUInt512(unsignedWithTail));
        Assert.Equal(signedValue, BitConverterEx.ToInt512(signedWithTail));
    }

    private static byte[] ExpectedBytes(UInt512 value)
    {
        var bytes = ((BigInteger)value).ToByteArray(isUnsigned: true, isBigEndian: false);
        return NormalizeToMachineEndianness(bytes);
    }

    private static byte[] ExpectedBytes(Int512 value)
    {
        var source = ((BigInteger)value).ToByteArray(isUnsigned: false, isBigEndian: false);
        byte[] bytes = new byte[64];
        Array.Copy(source, 0, bytes, 0, Math.Min(source.Length, 64));

        if ((BigInteger)value < 0 && source.Length < 64)
            Array.Fill(bytes, (byte)0xFF, source.Length, 64 - source.Length);

        if (!BitConverter.IsLittleEndian)
            Array.Reverse(bytes);

        return bytes;
    }

    private static byte[] NormalizeToMachineEndianness(byte[] littleEndian)
    {
        byte[] bytes = new byte[64];
        Array.Copy(littleEndian, 0, bytes, 0, Math.Min(littleEndian.Length, 64));

        if (!BitConverter.IsLittleEndian)
            Array.Reverse(bytes);

        return bytes;
    }
}
