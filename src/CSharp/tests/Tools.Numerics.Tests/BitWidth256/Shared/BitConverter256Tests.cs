using System.Numerics;
using Xunit;

namespace Tools.Numerics.Tests;

/// <summary>
/// Validates the 256-bit <see cref="BitConverterEx"/> overloads for behavior parity with core
/// <see cref="BitConverter"/> conventions.
/// </summary>
public sealed class BitConverterEx256Tests
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
    public void GetBytes_UInt256_UsesMachineEndiannessAndRoundTrips()
    {
        var values = new[]
        {
            UInt256.Zero,
            UInt256.One,
            UInt256.MaxValue,
            (UInt256)(BigInteger.One << 200),
            IntegerTestHelpers.RandomUInt256(new Random(1001))
        };

        foreach (var value in values)
        {
            var bytes = BitConverterEx.GetBytes(value);
            Assert.Equal(32, bytes.Length);
            Assert.Equal(ExpectedBytes(value), bytes);
            Assert.Equal(value, BitConverterEx.ToUInt256(bytes, 0));
            Assert.Equal(value, BitConverterEx.ToUInt256(bytes));
        }
    }

    /// <summary>
    /// Verifies that signed values are serialized using machine endianness and can be parsed back losslessly.
    /// </summary>
    [Fact]
    public void GetBytes_Int256_UsesMachineEndiannessAndRoundTrips()
    {
        var values = new[]
        {
            Int256.Zero,
            Int256.One,
            Int256.MinValue,
            Int256.MaxValue,
            (Int256)(-(BigInteger.One << 200)),
            IntegerTestHelpers.RandomInt256(new Random(2024))
        };

        foreach (var value in values)
        {
            var bytes = BitConverterEx.GetBytes(value);
            Assert.Equal(32, bytes.Length);
            Assert.Equal(ExpectedBytes(value), bytes);
            Assert.Equal(value, BitConverterEx.ToInt256(bytes, 0));
            Assert.Equal(value, BitConverterEx.ToInt256(bytes));
        }
    }

    /// <summary>
    /// Verifies span-writing behavior for both insufficient and sufficient destinations, including untouched trailing
    /// bytes.
    /// </summary>
    [Fact]
    public void TryWriteBytes_WritesAtBeginningAndReturnsLengthContract()
    {
        Span<byte> tooSmall = stackalloc byte[31];
        Assert.False(BitConverterEx.TryWriteBytes(tooSmall, UInt256.One));
        Assert.False(BitConverterEx.TryWriteBytes(tooSmall, Int256.One));

        byte[] target = new byte[64];
        target.AsSpan().Fill(0xCC);

        var uintValue = (UInt256)(BigInteger.One << 180) + 12345;
        var intValue = (Int256)(-(BigInteger.One << 180)) + 6789;

        Assert.True(BitConverterEx.TryWriteBytes(target.AsSpan(), uintValue));
        Assert.Equal(ExpectedBytes(uintValue), target[..32]);
        Assert.All(target[32..], b => Assert.Equal(0xCC, b));

        target.AsSpan().Fill(0xCC);
        Assert.True(BitConverterEx.TryWriteBytes(target.AsSpan(), intValue));
        Assert.Equal(ExpectedBytes(intValue), target[..32]);
        Assert.All(target[32..], b => Assert.Equal(0xCC, b));
    }

    /// <summary>
    /// Verifies that array overloads honor non-zero start indices when decoding 256-bit payloads.
    /// </summary>
    [Fact]
    public void To256_ArrayOverloads_RespectStartIndex()
    {
        var uintValue = IntegerTestHelpers.RandomUInt256(new Random(10));
        var intValue = IntegerTestHelpers.RandomInt256(new Random(11));

        var uintBytes = ExpectedBytes(uintValue);
        var intBytes = ExpectedBytes(intValue);

        byte[] container = new byte[80];
        new byte[] { 1, 2, 3, 4, 5 }.CopyTo(container, 0);
        uintBytes.CopyTo(container, 5);
        intBytes.CopyTo(container, 40);

        Assert.Equal(uintValue, BitConverterEx.ToUInt256(container, 5));
        Assert.Equal(intValue, BitConverterEx.ToInt256(container, 40));
    }

    /// <summary>
    /// Verifies guard-rail exceptions for null arrays, invalid indices, and arrays that are too short for 32-byte
    /// reads.
    /// </summary>
    [Fact]
    public void To256_ArrayOverloads_ThrowForInvalidInput()
    {
        Assert.Throws<ArgumentNullException>(() => BitConverterEx.ToUInt256(null!, 0));
        Assert.Throws<ArgumentNullException>(() => BitConverterEx.ToInt256(null!, 0));

        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToUInt256(new byte[32], -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToInt256(new byte[32], -1));

        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToUInt256(new byte[32], 32));
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToInt256(new byte[32], 32));

        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToUInt256(new byte[32], 33));
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToInt256(new byte[32], 33));

        Assert.Throws<ArgumentException>(() => BitConverterEx.ToUInt256(new byte[31], 0));
        Assert.Throws<ArgumentException>(() => BitConverterEx.ToInt256(new byte[31], 0));
    }

    /// <summary>
    /// Verifies that span overloads reject inputs shorter than the fixed 32-byte binary width.
    /// </summary>
    [Fact]
    public void To256_SpanOverloads_ThrowForShortInput()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToUInt256(new byte[31]));
        Assert.Throws<ArgumentOutOfRangeException>(() => BitConverterEx.ToInt256(new byte[31]));
    }
    /// <summary>
    /// Verifies span decoders read exactly the first 32 bytes and ignore trailing payload.
    /// </summary>
    [Fact]
    public void To256_SpanOverloads_ReadFixedWidthPrefix()
    {
        var uintValue = (UInt256)((BigInteger.One << 190) + 321);
        var intValue = (Int256)(-(BigInteger.One << 187) + 654);

        var uintBytes = ExpectedBytes(uintValue);
        var intBytes = ExpectedBytes(intValue);

        byte[] uintContainer = new byte[40];
        byte[] intContainer = new byte[48];
        uintBytes.CopyTo(uintContainer, 0);
        intBytes.CopyTo(intContainer, 0);
        Array.Fill(uintContainer, (byte)0xAA, 32, uintContainer.Length - 32);
        Array.Fill(intContainer, (byte)0x55, 32, intContainer.Length - 32);

        Assert.Equal(uintValue, BitConverterEx.ToUInt256(uintContainer));
        Assert.Equal(intValue, BitConverterEx.ToInt256(intContainer));
    }

    /// <summary>
    /// Verifies span decoding uses exactly the first 32 bytes and ignores any trailing payload bytes.
    /// </summary>
    [Fact]
    public void To256_SpanOverloads_IgnoreTrailingBytesBeyondFirst32()
    {
        var unsignedValue = IntegerTestHelpers.RandomUInt256(new Random(901));
        var signedValue = IntegerTestHelpers.RandomInt256(new Random(902));

        byte[] unsignedWithTail = new byte[64];
        byte[] expectedUnsigned = ExpectedBytes(unsignedValue);
        expectedUnsigned.CopyTo(unsignedWithTail, 0);
        Array.Fill(unsignedWithTail, (byte)0xCC, 32, 32);

        byte[] signedWithTail = new byte[64];
        byte[] expectedSigned = ExpectedBytes(signedValue);
        expectedSigned.CopyTo(signedWithTail, 0);
        Array.Fill(signedWithTail, (byte)0xDD, 32, 32);

        Assert.Equal(unsignedValue, BitConverterEx.ToUInt256(unsignedWithTail));
        Assert.Equal(signedValue, BitConverterEx.ToInt256(signedWithTail));
    }

    private static byte[] ExpectedBytes(UInt256 value)
    {
        var bytes = ((BigInteger)value).ToByteArray(isUnsigned: true, isBigEndian: false);
        return NormalizeToMachineEndianness(bytes);
    }

    private static byte[] ExpectedBytes(Int256 value)
    {
        var source = ((BigInteger)value).ToByteArray(isUnsigned: false, isBigEndian: false);
        byte[] bytes = new byte[32];
        Array.Copy(source, 0, bytes, 0, Math.Min(source.Length, 32));

        if ((BigInteger)value < 0 && source.Length < 32)
            Array.Fill(bytes, (byte)0xFF, source.Length, 32 - source.Length);

        if (!BitConverter.IsLittleEndian)
            Array.Reverse(bytes);

        return bytes;
    }

    private static byte[] NormalizeToMachineEndianness(byte[] littleEndian)
    {
        byte[] bytes = new byte[32];
        Array.Copy(littleEndian, 0, bytes, 0, Math.Min(littleEndian.Length, 32));

        if (!BitConverter.IsLittleEndian)
            Array.Reverse(bytes);

        return bytes;
    }
}
