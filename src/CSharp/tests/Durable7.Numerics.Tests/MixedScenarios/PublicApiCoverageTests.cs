using System.Globalization;
using System.Numerics;
using Xunit;

namespace Durable7.Numerics.Tests;

/// <summary>
/// Exercises every public API member exposed by <see cref="UInt256"/>, <see cref="Int256"/>,
/// <see cref="UInt512"/>, <see cref="Int512"/>, and <see cref="BitConverterEx"/> at least once to
/// prevent accidental blind spots when expanding functionality.
/// </summary>
/// <remarks>
/// <para>
/// This class intentionally concentrates on broad smoke coverage for public entry points that are easy to overlook
/// during refactors, such as checked operators, conversion operators, and byte-conversion APIs.
/// </para>
/// <para>
/// Deeper behavioral verification for arithmetic, parsing, formatting, and boundary semantics lives in the dedicated
/// type-specific suites (for example, <c>UInt512Tests</c> and <c>Int512Tests</c>).
/// </para>
/// </remarks>
public sealed class PublicApiCoverageTests
{
    /// <summary>
    /// Exercises the complete public <see cref="UInt256"/> surface to ensure each public member is directly invoked.
    /// </summary>
    [Fact]
    public void UInt256_PublicSurface_IsExercised()
    {
        var a = new UInt256(0, 123);
        var b = new UInt256(1, 456);
        var fromUlong = new UInt256(42UL);

        Assert.False(a.IsZero);
        Assert.True(UInt256.Zero.IsZero);
        Assert.Equal(0, a.CompareTo(a));
        Assert.True(a.Equals(a));
        Assert.True(a.Equals((object)a));
        Assert.Equal(a.GetHashCode(), a.GetHashCode());

        Assert.NotEmpty(a.ToString());
        Assert.NotEmpty(a.ToString("D", CultureInfo.InvariantCulture));

        Span<char> charBuffer = stackalloc char[128];
        Assert.True(a.TryFormat(charBuffer, out var charsWritten, default, CultureInfo.InvariantCulture));
        Assert.True(charsWritten > 0);

        Span<byte> utf8Buffer = stackalloc byte[128];
        Assert.True(a.TryFormat(utf8Buffer, out var bytesWritten, default, CultureInfo.InvariantCulture));
        Assert.True(bytesWritten > 0);

        Assert.Equal((UInt256)123, UInt256.Parse("123"));
        Assert.Equal((UInt256)123, UInt256.Parse("123", CultureInfo.InvariantCulture));
        Assert.Equal((UInt256)255, UInt256.Parse("ff", NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.Equal((UInt256)456, UInt256.Parse("456".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal((UInt256)789, UInt256.Parse("789"u8, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.True(UInt256.TryParse("123", out _));
        Assert.True(UInt256.TryParse("123", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(UInt256.TryParse("123".AsSpan(), out _));
        Assert.True(UInt256.TryParse("123".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(UInt256.TryParse("123"u8, out _));
        Assert.True(UInt256.TryParse("123"u8, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));

#pragma warning disable CS1718 // Intentional self-comparison verifies operator contract coverage.
        Assert.True(a == a);
#pragma warning restore CS1718
        Assert.True(a != b);
        Assert.True(a < b);
        Assert.True(b > a);
        Assert.True(a <= b);
        Assert.True(b >= a);

        Assert.Null(typeof(UInt256).GetMethod("op_UnaryNegation"));
        Assert.Null(typeof(UInt256).GetMethod("op_CheckedUnaryNegation"));

        _ = a + fromUlong;
        _ = +a;
        _ = b - a;
        _ = ~a;
        _ = a & b;
        _ = a | b;
        _ = a ^ b;
        _ = ++fromUlong;
        _ = --fromUlong;
        _ = a << 3;
        _ = b >> 2;
        _ = b >>> 2;
        _ = a * (UInt256)2;
        _ = b / (UInt256)2;
        _ = b % (UInt256)2;

        _ = checked(a + (UInt256)5);
        _ = checked(b - a);
        _ = checked(a * (UInt256)3);
        _ = checked(b / (UInt256)2);

        Assert.True(UInt256.LeadingZeroCount(a) >= 0);
        Assert.True(UInt256.TrailingZeroCount(a) >= 0);
        Assert.True(UInt256.PopCount(a) >= 0);
        _ = UInt256.RotateLeft(a, 7);
        _ = UInt256.RotateRight(a, 11);
        Assert.True(UInt256.Log2((UInt256)8) >= 0);

        Assert.Equal(32, a.GetByteCount());
        Assert.True(a.GetShortestBitLength() >= 1);

        var asBig = (BigInteger)a;
        var fromBig = (UInt256)asBig;
        Assert.Equal(a, fromBig);

        _ = (UInt256)(byte)1;
        _ = (UInt256)(ushort)1;
        _ = (UInt256)(uint)1;
        _ = (UInt256)(ulong)1;
        _ = (UInt256)(UInt128)1;

        var signedPositive = (Int256)123;
        _ = (UInt256)signedPositive;
        _ = checked((UInt256)signedPositive);

        _ = (byte)a;
        _ = checked((byte)(UInt256)255);
        _ = (ushort)a;
        _ = checked((ushort)(UInt256)255);
        _ = (uint)a;
        _ = checked((uint)(UInt256)255);
        _ = (ulong)a;
        _ = checked((ulong)(UInt256)255);
        _ = (UInt128)a;
        _ = checked((UInt128)(UInt256)255);
        _ = (sbyte)a;
        _ = checked((sbyte)(UInt256)100);
        _ = (short)a;
        _ = checked((short)(UInt256)100);
        _ = (int)a;
        _ = checked((int)(UInt256)100);
        _ = (long)a;
        _ = checked((long)(UInt256)100);
        _ = (Int128)a;
        _ = checked((Int128)(UInt256)100);

        Assert.Equal(0, ((IComparable)a).CompareTo(a));
    }

    /// <summary>
    /// Exercises the complete public <see cref="Int256"/> surface to ensure each public member is directly invoked.
    /// </summary>
    [Fact]
    public void Int256_PublicSurface_IsExercised()
    {
        var a = new Int256(0, 123);
        var b = new Int256(-456);

        Assert.False(a.IsZero);
        Assert.False(a.IsNegative);
        Assert.True(((Int256)(-1)).IsNegative);

        Assert.Equal(0, a.CompareTo(a));
        Assert.True(a.Equals(a));
        Assert.True(a.Equals((object)a));
        Assert.Equal(a.GetHashCode(), a.GetHashCode());

        Assert.NotEmpty(a.ToString());
        Assert.NotEmpty(a.ToString("D", CultureInfo.InvariantCulture));

        Span<char> charBuffer = stackalloc char[128];
        Assert.True(a.TryFormat(charBuffer, out var charsWritten, default, CultureInfo.InvariantCulture));
        Assert.True(charsWritten > 0);

        Span<byte> utf8Buffer = stackalloc byte[128];
        Assert.True(a.TryFormat(utf8Buffer, out var bytesWritten, default, CultureInfo.InvariantCulture));
        Assert.True(bytesWritten > 0);

        Assert.Equal(123, Int256.Parse("123"));
        Assert.Equal(123, Int256.Parse("123", CultureInfo.InvariantCulture));
        Assert.Equal(-255, Int256.Parse("-255", NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal(456, Int256.Parse("456".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal(789, Int256.Parse("789"u8, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.True(Int256.TryParse("123", out _));
        Assert.True(Int256.TryParse("123", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(Int256.TryParse("123".AsSpan(), out _));
        Assert.True(Int256.TryParse("123".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(Int256.TryParse("123"u8, out _));
        Assert.True(Int256.TryParse("123"u8, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));

#pragma warning disable CS1718 // Intentional self-comparison verifies operator contract coverage.
        Assert.True(a == a);
#pragma warning restore CS1718
        Assert.True(a != b);
        Assert.True(b < a);
        Assert.True(a > b);
        Assert.True(b <= a);
        Assert.True(a >= b);

        _ = a + b;
        _ = a - b;
        _ = ~a;
        _ = a & b;
        _ = a | b;
        _ = a ^ b;
        _ = -a;
        _ = +a;
        _ = ++a;
        _ = --a;
        _ = a << 3;
        _ = b >> 2;
        _ = b >>> 2;
        _ = a * 2;
        _ = a / 2;
        _ = a % 2;

        _ = checked(a + (Int256)5);
        _ = checked(a - (Int256)5);
        _ = checked(-(Int256)5);
        _ = checked((Int256)9 * (Int256)9);
        _ = checked((Int256)9 / (Int256)3);

        Assert.True(Int256.LeadingZeroCount(a) >= 0);
        Assert.True(Int256.TrailingZeroCount(a) >= 0);
        Assert.True(Int256.PopCount(a) >= 0);
        _ = Int256.RotateLeft(a, 7);
        _ = Int256.RotateRight(a, 11);
        Assert.Equal(123, Int256.Abs(-123));
        Assert.Equal(1, Int256.Sign(1));
        Assert.Equal(6, Int256.Log2(123));

        Assert.True(a.GetShortestBitLength() >= 1);
        Assert.Equal(32, a.GetByteCount());

        var asBig = (BigInteger)a;
        var fromBig = (Int256)asBig;
        Assert.Equal(a, fromBig);

        _ = (Int256)(sbyte)1;
        _ = (Int256)(short)1;
        _ = (Int256)1;
        _ = (Int256)(long)1;
        _ = (Int256)(byte)1;
        _ = (Int256)(ushort)1;
        _ = (Int256)(uint)1;
        _ = (Int256)(ulong)1;
        _ = (Int256)(Int128)1;
        _ = (Int256)(UInt128)1;

        var unsignedSmall = (UInt256)123;
        _ = (Int256)unsignedSmall;
        _ = checked((Int256)unsignedSmall);

        _ = (sbyte)a;
        _ = checked((sbyte)(Int256)100);
        _ = (byte)a;
        _ = checked((byte)(Int256)100);
        _ = (short)a;
        _ = checked((short)(Int256)100);
        _ = (ushort)a;
        _ = checked((ushort)(Int256)100);
        _ = (int)a;
        _ = checked((int)(Int256)100);
        _ = (uint)a;
        _ = checked((uint)(Int256)100);
        _ = (long)a;
        _ = checked((long)(Int256)100);
        _ = (ulong)a;
        _ = checked((ulong)(Int256)100);
        _ = (Int128)a;
        _ = checked((Int128)(Int256)100);
        _ = (UInt128)a;
        _ = checked((UInt128)(Int256)100);

        Assert.Equal(0, ((IComparable)a).CompareTo(a));
    }

    /// <summary>
    /// Exercises the complete public <see cref="UInt512"/> surface to ensure each public member is directly invoked.
    /// </summary>
    [Fact]
    public void UInt512_PublicSurface_IsExercised()
    {
        var a = new UInt512(0, 123);
        var b = new UInt512(1, 456);
        var fromUlong = new UInt512(42UL);

        Assert.False(a.IsZero);
        Assert.True(UInt512.Zero.IsZero);
        Assert.Equal(0, a.CompareTo(a));
        Assert.True(a.Equals(a));
        Assert.True(a.Equals((object)a));
        Assert.Equal(a.GetHashCode(), a.GetHashCode());

        Assert.NotEmpty(a.ToString());
        Assert.NotEmpty(a.ToString("D", CultureInfo.InvariantCulture));

        Span<char> charBuffer = stackalloc char[256];
        Assert.True(a.TryFormat(charBuffer, out var charsWritten, default, CultureInfo.InvariantCulture));
        Assert.True(charsWritten > 0);

        Span<byte> utf8Buffer = stackalloc byte[256];
        Assert.True(a.TryFormat(utf8Buffer, out var bytesWritten, default, CultureInfo.InvariantCulture));
        Assert.True(bytesWritten > 0);

        Assert.Equal((UInt512)123, UInt512.Parse("123"));
        Assert.Equal((UInt512)123, UInt512.Parse("123", CultureInfo.InvariantCulture));
        Assert.Equal((UInt512)255, UInt512.Parse("ff", NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.Equal((UInt512)456, UInt512.Parse("456".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal((UInt512)789, UInt512.Parse("789"u8, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.True(UInt512.TryParse("123", out _));
        Assert.True(UInt512.TryParse("123", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(UInt512.TryParse("123".AsSpan(), out _));
        Assert.True(UInt512.TryParse("123".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(UInt512.TryParse("123"u8, out _));
        Assert.True(UInt512.TryParse("123"u8, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));

#pragma warning disable CS1718 // Intentional self-comparison verifies operator contract coverage.
        Assert.True(a == a);
#pragma warning restore CS1718
        Assert.True(a != b);
        Assert.True(a < b);
        Assert.True(b > a);
        Assert.True(a <= b);
        Assert.True(b >= a);

        Assert.Null(typeof(UInt512).GetMethod("op_UnaryNegation"));
        Assert.Null(typeof(UInt512).GetMethod("op_CheckedUnaryNegation"));

        _ = a + fromUlong;
        _ = +a;
        _ = b - a;
        _ = ~a;
        _ = a & b;
        _ = a | b;
        _ = a ^ b;
        _ = ++fromUlong;
        _ = --fromUlong;
        _ = a << 3;
        _ = b >> 2;
        _ = b >>> 2;
        _ = a * (UInt512)2;
        _ = b / (UInt512)2;
        _ = b % (UInt512)2;

        _ = checked(a + (UInt512)5);
        _ = checked(b - a);
        _ = checked(a * (UInt512)3);
        _ = checked(b / (UInt512)2);

        Assert.True(UInt512.LeadingZeroCount(a) >= 0);
        Assert.True(UInt512.TrailingZeroCount(a) >= 0);
        Assert.True(UInt512.PopCount(a) >= 0);
        _ = UInt512.RotateLeft(a, 7);
        _ = UInt512.RotateRight(a, 11);
        Assert.True(UInt512.Log2((UInt512)8) >= 0);

        Assert.Equal(64, a.GetByteCount());
        Assert.True(a.GetShortestBitLength() >= 1);

        var asBig = (BigInteger)a;
        var fromBig = (UInt512)asBig;
        Assert.Equal(a, fromBig);
        _ = checked((UInt512)asBig);

        _ = (UInt512)(byte)1;
        _ = (UInt512)(ushort)1;
        _ = (UInt512)(uint)1;
        _ = (UInt512)(ulong)1;
        _ = (UInt512)(UInt256)1;

        var signedPositive = (Int512)123;
        _ = (UInt512)signedPositive;
        _ = checked((UInt512)signedPositive);

        _ = (byte)a;
        _ = checked((byte)(UInt512)255);
        _ = (ushort)a;
        _ = checked((ushort)(UInt512)255);
        _ = (uint)a;
        _ = checked((uint)(UInt512)255);
        _ = (ulong)a;
        _ = checked((ulong)(UInt512)255);
        _ = (UInt256)a;
        _ = checked((UInt256)(UInt512)255);
        _ = (sbyte)a;
        _ = checked((sbyte)(UInt512)100);
        _ = (short)a;
        _ = checked((short)(UInt512)100);
        _ = (int)a;
        _ = checked((int)(UInt512)100);
        _ = (long)a;
        _ = checked((long)(UInt512)100);
        _ = (Int256)a;
        _ = checked((Int256)(UInt512)100);

        Assert.Equal(0, ((IComparable)a).CompareTo(a));
    }

    /// <summary>
    /// Exercises the complete public <see cref="Int512"/> surface to ensure each public member is directly invoked.
    /// </summary>
    [Fact]
    public void Int512_PublicSurface_IsExercised()
    {
        var a = new Int512(0, 123);
        var b = new Int512(-456);

        Assert.False(a.IsZero);
        Assert.False(a.IsNegative);
        Assert.True(((Int512)(-1)).IsNegative);

        Assert.Equal(0, a.CompareTo(a));
        Assert.True(a.Equals(a));
        Assert.True(a.Equals((object)a));
        Assert.Equal(a.GetHashCode(), a.GetHashCode());

        Assert.NotEmpty(a.ToString());
        Assert.NotEmpty(a.ToString("D", CultureInfo.InvariantCulture));

        Span<char> charBuffer = stackalloc char[256];
        Assert.True(a.TryFormat(charBuffer, out var charsWritten, default, CultureInfo.InvariantCulture));
        Assert.True(charsWritten > 0);

        Span<byte> utf8Buffer = stackalloc byte[256];
        Assert.True(a.TryFormat(utf8Buffer, out var bytesWritten, default, CultureInfo.InvariantCulture));
        Assert.True(bytesWritten > 0);

        Assert.Equal(123, Int512.Parse("123"));
        Assert.Equal(123, Int512.Parse("123", CultureInfo.InvariantCulture));
        Assert.Equal(-255, Int512.Parse("-255", NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal(456, Int512.Parse("456".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal(789, Int512.Parse("789"u8, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.True(Int512.TryParse("123", out _));
        Assert.True(Int512.TryParse("123", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(Int512.TryParse("123".AsSpan(), out _));
        Assert.True(Int512.TryParse("123".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(Int512.TryParse("123"u8, out _));
        Assert.True(Int512.TryParse("123"u8, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));

#pragma warning disable CS1718 // Intentional self-comparison verifies operator contract coverage.
        Assert.True(a == a);
#pragma warning restore CS1718
        Assert.True(a != b);
        Assert.True(b < a);
        Assert.True(a > b);
        Assert.True(b <= a);
        Assert.True(a >= b);

        _ = a + b;
        _ = a - b;
        _ = ~a;
        _ = a & b;
        _ = a | b;
        _ = a ^ b;
        _ = -a;
        _ = +a;
        _ = ++a;
        _ = --a;
        _ = a << 3;
        _ = b >> 2;
        _ = b >>> 2;
        _ = a * 2;
        _ = a / 2;
        _ = a % 2;

        _ = checked(a + (Int512)5);
        _ = checked(a - (Int512)5);
        _ = checked(-(Int512)5);
        _ = checked((Int512)9 * (Int512)9);
        _ = checked((Int512)9 / (Int512)3);

        Assert.True(Int512.LeadingZeroCount(a) >= 0);
        Assert.True(Int512.TrailingZeroCount(a) >= 0);
        Assert.True(Int512.PopCount(a) >= 0);
        _ = Int512.RotateLeft(a, 7);
        _ = Int512.RotateRight(a, 11);
        Assert.Equal(123, Int512.Abs(-123));
        Assert.Equal(1, Int512.Sign(1));
        Assert.Equal(6, Int512.Log2(123));

        Assert.True(a.GetShortestBitLength() >= 1);
        Assert.Equal(64, a.GetByteCount());

        var asBig = (BigInteger)a;
        var fromBig = (Int512)asBig;
        Assert.Equal(a, fromBig);
        _ = checked((Int512)asBig);

        _ = (Int512)(sbyte)1;
        _ = (Int512)(short)1;
        _ = (Int512)1;
        _ = (Int512)(long)1;
        _ = (Int512)(byte)1;
        _ = (Int512)(ushort)1;
        _ = (Int512)(uint)1;
        _ = (Int512)(ulong)1;
        _ = (Int512)(Int256)1;
        _ = (Int512)(UInt256)1;

        var unsignedSmall = (UInt512)123;
        _ = (Int512)unsignedSmall;
        _ = checked((Int512)unsignedSmall);

        _ = (sbyte)a;
        _ = checked((sbyte)(Int512)100);
        _ = (byte)a;
        _ = checked((byte)(Int512)100);
        _ = (short)a;
        _ = checked((short)(Int512)100);
        _ = (ushort)a;
        _ = checked((ushort)(Int512)100);
        _ = (int)a;
        _ = checked((int)(Int512)100);
        _ = (uint)a;
        _ = checked((uint)(Int512)100);
        _ = (long)a;
        _ = checked((long)(Int512)100);
        _ = (ulong)a;
        _ = checked((ulong)(Int512)100);
        _ = (Int256)a;
        _ = checked((Int256)(Int512)100);
        _ = (UInt256)a;
        _ = checked((UInt256)(Int512)100);

        Assert.Equal(0, ((IComparable)a).CompareTo(a));
    }

    /// <summary>
    /// Exercises every public member on <see cref="BitConverterEx"/> to catch accidental API regressions.
    /// </summary>
    [Fact]
    public void BitConverterEx_PublicSurface_IsExercised()
    {
        Assert.Equal(BitConverter.IsLittleEndian, BitConverterEx.IsLittleEndian);

        var unsigned = (UInt256)((BigInteger.One << 200) + 1234);
        var signed = (Int256)(-(BigInteger.One << 180) + 55);

        var uBytes = BitConverterEx.GetBytes(unsigned);
        var iBytes = BitConverterEx.GetBytes(signed);

        Assert.Equal(32, uBytes.Length);
        Assert.Equal(32, iBytes.Length);

        Span<byte> uDestination = stackalloc byte[32];
        Span<byte> iDestination = stackalloc byte[32];
        Assert.True(BitConverterEx.TryWriteBytes(uDestination, unsigned));
        Assert.True(BitConverterEx.TryWriteBytes(iDestination, signed));

        Assert.Equal(unsigned, BitConverterEx.ToUInt256(uBytes, 0));
        Assert.Equal(signed, BitConverterEx.ToInt256(iBytes, 0));
        Assert.Equal(unsigned, BitConverterEx.ToUInt256(uBytes));
        Assert.Equal(signed, BitConverterEx.ToInt256(iBytes));

        var unsigned512 = (UInt512)((BigInteger.One << 400) + 4321);
        var signed512 = (Int512)(-(BigInteger.One << 380) + 77);

        var u512Bytes = BitConverterEx.GetBytes(unsigned512);
        var i512Bytes = BitConverterEx.GetBytes(signed512);

        Assert.Equal(64, u512Bytes.Length);
        Assert.Equal(64, i512Bytes.Length);

        Span<byte> u512Destination = stackalloc byte[64];
        Span<byte> i512Destination = stackalloc byte[64];
        Assert.True(BitConverterEx.TryWriteBytes(u512Destination, unsigned512));
        Assert.True(BitConverterEx.TryWriteBytes(i512Destination, signed512));

        Assert.Equal(unsigned512, BitConverterEx.ToUInt512(u512Bytes, 0));
        Assert.Equal(signed512, BitConverterEx.ToInt512(i512Bytes, 0));
        Assert.Equal(unsigned512, BitConverterEx.ToUInt512(u512Bytes));
        Assert.Equal(signed512, BitConverterEx.ToInt512(i512Bytes));

        var unsigned1024 = (UInt1024)((BigInteger.One << 700) + 9876);
        var signed1024 = (Int1024)(-(BigInteger.One << 680) + 111);

        var u1024Bytes = BitConverterEx.GetBytes(unsigned1024);
        var i1024Bytes = BitConverterEx.GetBytes(signed1024);

        Assert.Equal(128, u1024Bytes.Length);
        Assert.Equal(128, i1024Bytes.Length);

        Span<byte> u1024Destination = stackalloc byte[128];
        Span<byte> i1024Destination = stackalloc byte[128];
        Assert.True(BitConverterEx.TryWriteBytes(u1024Destination, unsigned1024));
        Assert.True(BitConverterEx.TryWriteBytes(i1024Destination, signed1024));

        Assert.Equal(unsigned1024, BitConverterEx.ToUInt1024(u1024Bytes, 0));
        Assert.Equal(signed1024, BitConverterEx.ToInt1024(i1024Bytes, 0));
        Assert.Equal(unsigned1024, BitConverterEx.ToUInt1024(u1024Bytes));
        Assert.Equal(signed1024, BitConverterEx.ToInt1024(i1024Bytes));
    }

    /// <summary>
    /// Exercises the complete public <see cref="UInt1024"/> surface to ensure each public member is directly invoked.
    /// </summary>
    [Fact]
    public void UInt1024_PublicSurface_IsExercised()
    {
        var a = new UInt1024(0, 123);
        var b = new UInt1024(1, 456);
        var fromUlong = new UInt1024(42UL);

        Assert.False(a.IsZero);
        Assert.True(UInt1024.Zero.IsZero);
        Assert.Equal(0, a.CompareTo(a));
        Assert.True(a.Equals(a));
        Assert.True(a.Equals((object)a));
        Assert.Equal(a.GetHashCode(), a.GetHashCode());

        Assert.NotEmpty(a.ToString());
        Assert.NotEmpty(a.ToString("D", CultureInfo.InvariantCulture));

        Span<char> charBuffer = stackalloc char[256];
        Assert.True(a.TryFormat(charBuffer, out var charsWritten, default, CultureInfo.InvariantCulture));
        Assert.True(charsWritten > 0);

        Span<byte> utf8Buffer = stackalloc byte[256];
        Assert.True(a.TryFormat(utf8Buffer, out var bytesWritten, default, CultureInfo.InvariantCulture));
        Assert.True(bytesWritten > 0);

        Assert.Equal((UInt1024)123, UInt1024.Parse("123"));
        Assert.Equal((UInt1024)123, UInt1024.Parse("123", CultureInfo.InvariantCulture));
        Assert.Equal((UInt1024)255, UInt1024.Parse("ff", NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture));
        Assert.Equal((UInt1024)456, UInt1024.Parse("456".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal((UInt1024)789, UInt1024.Parse("789"u8, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.True(UInt1024.TryParse("123", out _));
        Assert.True(UInt1024.TryParse("123", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(UInt1024.TryParse("123".AsSpan(), out _));
        Assert.True(UInt1024.TryParse("123".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(UInt1024.TryParse("123"u8, out _));
        Assert.True(UInt1024.TryParse("123"u8, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));

#pragma warning disable CS1718 // Intentional self-comparison verifies operator contract coverage.
        Assert.True(a == a);
#pragma warning restore CS1718
        Assert.True(a != b);
        Assert.True(a < b);
        Assert.True(b > a);
        Assert.True(a <= b);
        Assert.True(b >= a);

        Assert.Null(typeof(UInt1024).GetMethod("op_UnaryNegation"));
        Assert.Null(typeof(UInt1024).GetMethod("op_CheckedUnaryNegation"));

        _ = a + fromUlong;
        _ = +a;
        _ = b - a;
        _ = ~a;
        _ = a & b;
        _ = a | b;
        _ = a ^ b;
        _ = ++fromUlong;
        _ = --fromUlong;
        _ = a << 3;
        _ = b >> 2;
        _ = b >>> 2;
        _ = a * (UInt1024)2;
        _ = b / (UInt1024)2;
        _ = b % (UInt1024)2;

        _ = checked(a + (UInt1024)5);
        _ = checked(b - a);
        _ = checked(a * (UInt1024)3);
        _ = checked(b / (UInt1024)2);

        Assert.True(UInt1024.LeadingZeroCount(a) >= 0);
        Assert.True(UInt1024.TrailingZeroCount(a) >= 0);
        Assert.True(UInt1024.PopCount(a) >= 0);
        _ = UInt1024.RotateLeft(a, 7);
        _ = UInt1024.RotateRight(a, 11);
        Assert.True(UInt1024.Log2((UInt1024)8) >= 0);

        Assert.Equal(128, a.GetByteCount());
        Assert.True(a.GetShortestBitLength() >= 1);

        var asBig = (BigInteger)a;
        var fromBig = (UInt1024)asBig;
        Assert.Equal(a, fromBig);
        _ = checked((UInt1024)asBig);

        _ = (UInt1024)(byte)1;
        _ = (UInt1024)(ushort)1;
        _ = (UInt1024)(uint)1;
        _ = (UInt1024)(ulong)1;
        _ = (UInt1024)(UInt512)1;

        var signedPositive = (Int1024)123;
        _ = (UInt1024)signedPositive;
        _ = checked((UInt1024)signedPositive);

        _ = (byte)a;
        _ = checked((byte)(UInt1024)255);
        _ = (ushort)a;
        _ = checked((ushort)(UInt1024)255);
        _ = (uint)a;
        _ = checked((uint)(UInt1024)255);
        _ = (ulong)a;
        _ = checked((ulong)(UInt1024)255);
        _ = (UInt512)a;
        _ = checked((UInt512)(UInt1024)255);
        _ = (sbyte)a;
        _ = checked((sbyte)(UInt1024)100);
        _ = (short)a;
        _ = checked((short)(UInt1024)100);
        _ = (int)a;
        _ = checked((int)(UInt1024)100);
        _ = (long)a;
        _ = checked((long)(UInt1024)100);
        _ = (Int512)a;
        _ = checked((Int512)(UInt1024)100);

        Assert.Equal(0, ((IComparable)a).CompareTo(a));
    }

    /// <summary>
    /// Exercises the complete public <see cref="Int1024"/> surface to ensure each public member is directly invoked.
    /// </summary>
    [Fact]
    public void Int1024_PublicSurface_IsExercised()
    {
        var a = new Int1024(0, 123);
        var b = new Int1024(-456);

        Assert.False(a.IsZero);
        Assert.False(a.IsNegative);
        Assert.True(((Int1024)(-1)).IsNegative);

        Assert.Equal(0, a.CompareTo(a));
        Assert.True(a.Equals(a));
        Assert.True(a.Equals((object)a));
        Assert.Equal(a.GetHashCode(), a.GetHashCode());

        Assert.NotEmpty(a.ToString());
        Assert.NotEmpty(a.ToString("D", CultureInfo.InvariantCulture));

        Span<char> charBuffer = stackalloc char[256];
        Assert.True(a.TryFormat(charBuffer, out var charsWritten, default, CultureInfo.InvariantCulture));
        Assert.True(charsWritten > 0);

        Span<byte> utf8Buffer = stackalloc byte[256];
        Assert.True(a.TryFormat(utf8Buffer, out var bytesWritten, default, CultureInfo.InvariantCulture));
        Assert.True(bytesWritten > 0);

        Assert.Equal(123, Int1024.Parse("123"));
        Assert.Equal(123, Int1024.Parse("123", CultureInfo.InvariantCulture));
        Assert.Equal(-255, Int1024.Parse("-255", NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal(456, Int1024.Parse("456".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture));
        Assert.Equal(789, Int1024.Parse("789"u8, NumberStyles.Integer, CultureInfo.InvariantCulture));

        Assert.True(Int1024.TryParse("123", out _));
        Assert.True(Int1024.TryParse("123", NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(Int1024.TryParse("123".AsSpan(), out _));
        Assert.True(Int1024.TryParse("123".AsSpan(), NumberStyles.Integer, CultureInfo.InvariantCulture, out _));
        Assert.True(Int1024.TryParse("123"u8, out _));
        Assert.True(Int1024.TryParse("123"u8, NumberStyles.Integer, CultureInfo.InvariantCulture, out _));

#pragma warning disable CS1718 // Intentional self-comparison verifies operator contract coverage.
        Assert.True(a == a);
#pragma warning restore CS1718
        Assert.True(a != b);
        Assert.True(b < a);
        Assert.True(a > b);
        Assert.True(b <= a);
        Assert.True(a >= b);

        _ = a + b;
        _ = a - b;
        _ = ~a;
        _ = a & b;
        _ = a | b;
        _ = a ^ b;
        _ = -a;
        _ = +a;
        _ = ++a;
        _ = --a;
        _ = a << 3;
        _ = b >> 2;
        _ = b >>> 2;
        _ = a * 2;
        _ = a / 2;
        _ = a % 2;

        _ = checked(a + (Int1024)5);
        _ = checked(a - (Int1024)5);
        _ = checked(-(Int1024)5);
        _ = checked((Int1024)9 * (Int1024)9);
        _ = checked((Int1024)9 / (Int1024)3);

        Assert.True(Int1024.LeadingZeroCount(a) >= 0);
        Assert.True(Int1024.TrailingZeroCount(a) >= 0);
        Assert.True(Int1024.PopCount(a) >= 0);
        _ = Int1024.RotateLeft(a, 7);
        _ = Int1024.RotateRight(a, 11);
        Assert.Equal(123, Int1024.Abs(-123));
        Assert.Equal(1, Int1024.Sign(1));
        Assert.Equal(6, Int1024.Log2(123));

        Assert.True(a.GetShortestBitLength() >= 1);
        Assert.Equal(128, a.GetByteCount());

        var asBig = (BigInteger)a;
        var fromBig = (Int1024)asBig;
        Assert.Equal(a, fromBig);
        _ = checked((Int1024)asBig);

        _ = (Int1024)(sbyte)1;
        _ = (Int1024)(short)1;
        _ = (Int1024)1;
        _ = (Int1024)(long)1;
        _ = (Int1024)(byte)1;
        _ = (Int1024)(ushort)1;
        _ = (Int1024)(uint)1;
        _ = (Int1024)(ulong)1;
        _ = (Int1024)(Int512)1;
        _ = (Int1024)(UInt512)1;

        var unsignedSmall = (UInt1024)123;
        _ = (Int1024)unsignedSmall;
        _ = checked((Int1024)unsignedSmall);

        _ = (sbyte)a;
        _ = checked((sbyte)(Int1024)100);
        _ = (byte)a;
        _ = checked((byte)(Int1024)100);
        _ = (short)a;
        _ = checked((short)(Int1024)100);
        _ = (ushort)a;
        _ = checked((ushort)(Int1024)100);
        _ = (int)a;
        _ = checked((int)(Int1024)100);
        _ = (uint)a;
        _ = checked((uint)(Int1024)100);
        _ = (long)a;
        _ = checked((long)(Int1024)100);
        _ = (ulong)a;
        _ = checked((ulong)(Int1024)100);
        _ = (Int512)a;
        _ = checked((Int512)(Int1024)100);
        _ = (UInt512)a;
        _ = checked((UInt512)(Int1024)100);

        Assert.Equal(0, ((IComparable)a).CompareTo(a));
    }

}
