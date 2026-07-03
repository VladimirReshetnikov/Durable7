// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Tools.Numerics;

/// <summary>
/// Provides conversions between fixed-width integer values and binary buffers.
/// </summary>
/// <remarks>
/// <para>
/// The API mirrors the shape and behavior of <see cref="BitConverter"/> for framework primitives, extended for
/// <see cref="Int256"/>, <see cref="UInt256"/>, <see cref="Int512"/>, <see cref="UInt512"/>, <see cref="Int1024"/>, and <see cref="UInt1024"/>.
/// </para>
/// <para>
/// Methods that produce bytes emit values in the current architecture endianness, just like
/// <see cref="BitConverter"/>. On little-endian platforms this means least-significant byte first.
/// </para>
/// <para>
/// Signed values use two's-complement encoding.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// UInt256 value = UInt256.Parse("340282366920938463463374607431768211456");
/// byte[] bytes = BitConverterEx.GetBytes(value);
/// UInt256 roundTrip = BitConverterEx.ToUInt256(bytes);
/// // roundTrip == value
/// </code>
/// </example>
public static class BitConverterEx
{
    private const int ByteCount256 = 32;
    private const int ByteCount512 = 64;
    private const int ByteCount1024 = 128;

    /// <summary>
    /// Gets a value indicating whether the current architecture stores values with the least-significant byte first.
    /// </summary>
    public static readonly bool IsLittleEndian = BitConverter.IsLittleEndian;

    /// <summary>
    /// Returns the specified unsigned 256-bit integer as a 32-byte array.
    /// </summary>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// A 32-byte array containing <paramref name="value"/> encoded in the current architecture endianness.
    /// </returns>
    public static byte[] GetBytes(UInt256 value)
    {
        byte[] bytes = new byte[ByteCount256];
        WriteUInt256EndianAware(value, bytes);
        return bytes;
    }

    /// <summary>
    /// Returns the specified signed 256-bit integer as a 32-byte array.
    /// </summary>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// A 32-byte array containing <paramref name="value"/> encoded in the current architecture endianness.
    /// </returns>
    public static byte[] GetBytes(Int256 value)
    {
        byte[] bytes = new byte[ByteCount256];
        WriteInt256EndianAware(value, bytes);
        return bytes;
    }

    /// <summary>
    /// Writes the specified unsigned 256-bit integer into a destination span.
    /// </summary>
    /// <param name="destination">
    /// The destination span that must provide at least 32 bytes. Only the first 32 bytes are written.
    /// </param>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="destination"/> is at least 32 bytes; otherwise, <see langword="false"/>.
    /// </returns>
    /// <example>
    /// <code>
    /// Span&lt;byte&gt; destination = stackalloc byte[32];
    /// bool ok = BitConverterEx.TryWriteBytes(destination, UInt256.One);
    /// // ok == true and destination[0] == 1 on little-endian machines.
    /// </code>
    /// </example>
    public static bool TryWriteBytes(Span<byte> destination, UInt256 value)
    {
        if (destination.Length < ByteCount256)
            return false;

        WriteUInt256EndianAware(value, destination);
        return true;
    }

    /// <summary>
    /// Writes the specified signed 256-bit integer into a destination span.
    /// </summary>
    /// <param name="destination">
    /// The destination span that must provide at least 32 bytes. Only the first 32 bytes are written.
    /// </param>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="destination"/> is at least 32 bytes; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryWriteBytes(Span<byte> destination, Int256 value)
    {
        if (destination.Length < ByteCount256)
            return false;

        WriteInt256EndianAware(value, destination);
        return true;
    }

    /// <summary>
    /// Returns an unsigned 256-bit integer converted from a byte array at a specified starting position.
    /// </summary>
    /// <param name="value">The input byte array.</param>
    /// <param name="startIndex">The starting position within <paramref name="value"/>.</param>
    /// <returns>
    /// The unsigned 256-bit integer represented by 32 bytes at <paramref name="startIndex"/>, interpreted in
    /// machine endianness.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="value"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="startIndex"/> is outside the bounds of <paramref name="value"/>.
    /// </exception>
    /// <exception cref="ArgumentException">
    /// Fewer than 32 bytes remain starting at <paramref name="startIndex"/>.
    /// </exception>
    public static UInt256 ToUInt256(byte[] value, int startIndex)
    {
        ArgumentNullException.ThrowIfNull(value);

        return (uint)startIndex >= (uint)value.Length
            ? throw new ArgumentOutOfRangeException(nameof(startIndex))
            : value.Length - startIndex < ByteCount256
                ? throw new ArgumentException("At least 32 bytes are required from startIndex.", nameof(value))
                : ToUInt256(value.AsSpan(startIndex, ByteCount256));
    }

    /// <summary>
    /// Returns a signed 256-bit integer converted from a byte array at a specified starting position.
    /// </summary>
    /// <param name="value">The input byte array.</param>
    /// <param name="startIndex">The starting position within <paramref name="value"/>.</param>
    /// <returns>
    /// The signed 256-bit integer represented by 32 bytes at <paramref name="startIndex"/>, interpreted in machine
    /// endianness.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="value"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="startIndex"/> is outside the bounds of <paramref name="value"/>.
    /// </exception>
    /// <exception cref="ArgumentException">
    /// Fewer than 32 bytes remain starting at <paramref name="startIndex"/>.
    /// </exception>
    public static Int256 ToInt256(byte[] value, int startIndex)
    {
        ArgumentNullException.ThrowIfNull(value);

        return (uint)startIndex >= (uint)value.Length
            ? throw new ArgumentOutOfRangeException(nameof(startIndex))
            : value.Length - startIndex < ByteCount256
                ? throw new ArgumentException("At least 32 bytes are required from startIndex.", nameof(value))
                : ToInt256(value.AsSpan(startIndex, ByteCount256));
    }

    /// <summary>
    /// Returns an unsigned 256-bit integer converted from a read-only byte span.
    /// </summary>
    /// <param name="value">The source bytes containing the encoded value.</param>
    /// <returns>
    /// The unsigned 256-bit integer represented by the first 32 bytes of <paramref name="value"/>, interpreted in
    /// machine endianness.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="value"/> contains fewer than 32 bytes.</exception>
    /// <remarks>
    /// If <paramref name="value"/> is longer than 32 bytes, trailing bytes are ignored. This matches the fixed-width
    /// contract used across all <c>ToUInt*</c> and <c>ToInt*</c> span overloads in this type.
    /// </remarks>
    public static UInt256 ToUInt256(ReadOnlySpan<byte> value)
    {
        if (value.Length < ByteCount256)
            throw new ArgumentOutOfRangeException(nameof(value), "At least 32 bytes are required.");

        if (IsLittleEndian)
            return (UInt256)new BigInteger(value[..ByteCount256], isUnsigned: true, isBigEndian: false);

        Span<byte> little = stackalloc byte[ByteCount256];
        value[..ByteCount256].CopyTo(little);
        little.Reverse();
        return (UInt256)new BigInteger(little, isUnsigned: true, isBigEndian: false);
    }

    /// <summary>
    /// Returns a signed 256-bit integer converted from a read-only byte span.
    /// </summary>
    /// <param name="value">The source bytes containing the encoded value.</param>
    /// <returns>
    /// The signed 256-bit integer represented by the first 32 bytes of <paramref name="value"/>, interpreted in
    /// machine endianness.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="value"/> contains fewer than 32 bytes.</exception>
    public static Int256 ToInt256(ReadOnlySpan<byte> value)
    {
        if (value.Length < ByteCount256)
            throw new ArgumentOutOfRangeException(nameof(value), "At least 32 bytes are required.");

        if (IsLittleEndian)
            return (Int256)new BigInteger(value[..ByteCount256], isUnsigned: false, isBigEndian: false);

        Span<byte> little = stackalloc byte[ByteCount256];
        value[..ByteCount256].CopyTo(little);
        little.Reverse();
        return (Int256)new BigInteger(little, isUnsigned: false, isBigEndian: false);
    }

    /// <summary>
    /// Returns the specified unsigned 512-bit integer as a 64-byte array.
    /// </summary>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// A 64-byte array containing <paramref name="value"/> encoded in the current architecture endianness.
    /// </returns>
    public static byte[] GetBytes(UInt512 value)
    {
        byte[] bytes = new byte[ByteCount512];
        WriteUInt512EndianAware(value, bytes);
        return bytes;
    }

    /// <summary>
    /// Returns the specified signed 512-bit integer as a 64-byte array.
    /// </summary>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// A 64-byte array containing <paramref name="value"/> encoded in the current architecture endianness.
    /// </returns>
    public static byte[] GetBytes(Int512 value)
    {
        byte[] bytes = new byte[ByteCount512];
        WriteInt512EndianAware(value, bytes);
        return bytes;
    }

    /// <summary>
    /// Writes the specified unsigned 512-bit integer into a destination span.
    /// </summary>
    /// <param name="destination">
    /// The destination span that must provide at least 64 bytes. Only the first 64 bytes are written.
    /// </param>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="destination"/> is at least 64 bytes; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryWriteBytes(Span<byte> destination, UInt512 value)
    {
        if (destination.Length < ByteCount512)
            return false;

        WriteUInt512EndianAware(value, destination);
        return true;
    }

    /// <summary>
    /// Writes the specified signed 512-bit integer into a destination span.
    /// </summary>
    /// <param name="destination">
    /// The destination span that must provide at least 64 bytes. Only the first 64 bytes are written.
    /// </param>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="destination"/> is at least 64 bytes; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryWriteBytes(Span<byte> destination, Int512 value)
    {
        if (destination.Length < ByteCount512)
            return false;

        WriteInt512EndianAware(value, destination);
        return true;
    }

    /// <summary>
    /// Returns an unsigned 512-bit integer converted from a byte array at a specified starting position.
    /// </summary>
    /// <param name="value">The input byte array.</param>
    /// <param name="startIndex">The starting position within <paramref name="value"/>.</param>
    /// <returns>
    /// The unsigned 512-bit integer represented by 64 bytes at <paramref name="startIndex"/>, interpreted in
    /// machine endianness.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="value"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="startIndex"/> is outside the bounds of <paramref name="value"/>.
    /// </exception>
    /// <exception cref="ArgumentException">
    /// Fewer than 64 bytes remain starting at <paramref name="startIndex"/>.
    /// </exception>
    public static UInt512 ToUInt512(byte[] value, int startIndex)
    {
        ArgumentNullException.ThrowIfNull(value);

        return (uint)startIndex >= (uint)value.Length
            ? throw new ArgumentOutOfRangeException(nameof(startIndex))
            : value.Length - startIndex < ByteCount512
                ? throw new ArgumentException("At least 64 bytes are required from startIndex.", nameof(value))
                : ToUInt512(value.AsSpan(startIndex, ByteCount512));
    }

    /// <summary>
    /// Returns a signed 512-bit integer converted from a byte array at a specified starting position.
    /// </summary>
    /// <param name="value">The input byte array.</param>
    /// <param name="startIndex">The starting position within <paramref name="value"/>.</param>
    /// <returns>
    /// The signed 512-bit integer represented by 64 bytes at <paramref name="startIndex"/>, interpreted in machine
    /// endianness.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="value"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="startIndex"/> is outside the bounds of <paramref name="value"/>.
    /// </exception>
    /// <exception cref="ArgumentException">
    /// Fewer than 64 bytes remain starting at <paramref name="startIndex"/>.
    /// </exception>
    public static Int512 ToInt512(byte[] value, int startIndex)
    {
        ArgumentNullException.ThrowIfNull(value);

        return (uint)startIndex >= (uint)value.Length
            ? throw new ArgumentOutOfRangeException(nameof(startIndex))
            : value.Length - startIndex < ByteCount512
                ? throw new ArgumentException("At least 64 bytes are required from startIndex.", nameof(value))
                : ToInt512(value.AsSpan(startIndex, ByteCount512));
    }

    /// <summary>
    /// Returns an unsigned 512-bit integer converted from a read-only byte span.
    /// </summary>
    /// <param name="value">The source bytes containing the encoded value.</param>
    /// <returns>
    /// The unsigned 512-bit integer represented by the first 64 bytes of <paramref name="value"/>, interpreted in
    /// machine endianness.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="value"/> contains fewer than 64 bytes.</exception>
    public static UInt512 ToUInt512(ReadOnlySpan<byte> value)
    {
        if (value.Length < ByteCount512)
            throw new ArgumentOutOfRangeException(nameof(value), "At least 64 bytes are required.");

        if (IsLittleEndian)
            return (UInt512)new BigInteger(value[..ByteCount512], isUnsigned: true, isBigEndian: false);

        Span<byte> little = stackalloc byte[ByteCount512];
        value[..ByteCount512].CopyTo(little);
        little.Reverse();
        return (UInt512)new BigInteger(little, isUnsigned: true, isBigEndian: false);
    }

    /// <summary>
    /// Returns a signed 512-bit integer converted from a read-only byte span.
    /// </summary>
    /// <param name="value">The source bytes containing the encoded value.</param>
    /// <returns>
    /// The signed 512-bit integer represented by the first 64 bytes of <paramref name="value"/>, interpreted in
    /// machine endianness.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="value"/> contains fewer than 64 bytes.</exception>
    public static Int512 ToInt512(ReadOnlySpan<byte> value)
    {
        if (value.Length < ByteCount512)
            throw new ArgumentOutOfRangeException(nameof(value), "At least 64 bytes are required.");

        if (IsLittleEndian)
            return (Int512)new BigInteger(value[..ByteCount512], isUnsigned: false, isBigEndian: false);

        Span<byte> little = stackalloc byte[ByteCount512];
        value[..ByteCount512].CopyTo(little);
        little.Reverse();
        return (Int512)new BigInteger(little, isUnsigned: false, isBigEndian: false);
    }

    /// <summary>
    /// Returns the specified unsigned 1024-bit integer as a 128-byte array.
    /// </summary>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// A 128-byte array containing <paramref name="value"/> encoded in the current architecture endianness.
    /// </returns>
    public static byte[] GetBytes(UInt1024 value)
    {
        byte[] bytes = new byte[ByteCount1024];
        WriteUInt1024EndianAware(value, bytes);
        return bytes;
    }

    /// <summary>
    /// Returns the specified signed 1024-bit integer as a 128-byte array.
    /// </summary>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// A 128-byte array containing <paramref name="value"/> encoded in the current architecture endianness.
    /// </returns>
    public static byte[] GetBytes(Int1024 value)
    {
        byte[] bytes = new byte[ByteCount1024];
        WriteInt1024EndianAware(value, bytes);
        return bytes;
    }

    /// <summary>
    /// Writes the specified unsigned 1024-bit integer into a destination span.
    /// </summary>
    /// <param name="destination">
    /// The destination span that must provide at least 128 bytes. Only the first 128 bytes are written.
    /// </param>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="destination"/> is at least 128 bytes; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryWriteBytes(Span<byte> destination, UInt1024 value)
    {
        if (destination.Length < ByteCount1024)
            return false;

        WriteUInt1024EndianAware(value, destination);
        return true;
    }

    /// <summary>
    /// Writes the specified signed 1024-bit integer into a destination span.
    /// </summary>
    /// <param name="destination">
    /// The destination span that must provide at least 128 bytes. Only the first 128 bytes are written.
    /// </param>
    /// <param name="value">The value to convert.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="destination"/> is at least 128 bytes; otherwise, <see langword="false"/>.
    /// </returns>
    public static bool TryWriteBytes(Span<byte> destination, Int1024 value)
    {
        if (destination.Length < ByteCount1024)
            return false;

        WriteInt1024EndianAware(value, destination);
        return true;
    }

    /// <summary>
    /// Returns an unsigned 1024-bit integer converted from a byte array at a specified starting position.
    /// </summary>
    /// <param name="value">The input byte array.</param>
    /// <param name="startIndex">The starting position within <paramref name="value"/>.</param>
    /// <returns>
    /// The unsigned 1024-bit integer represented by 128 bytes at <paramref name="startIndex"/>, interpreted in
    /// machine endianness.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="value"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="startIndex"/> is outside the bounds of <paramref name="value"/>.
    /// </exception>
    /// <exception cref="ArgumentException">
    /// Fewer than 128 bytes remain starting at <paramref name="startIndex"/>.
    /// </exception>
    public static UInt1024 ToUInt1024(byte[] value, int startIndex)
    {
        ArgumentNullException.ThrowIfNull(value);

        return (uint)startIndex >= (uint)value.Length
            ? throw new ArgumentOutOfRangeException(nameof(startIndex))
            : value.Length - startIndex < ByteCount1024
                ? throw new ArgumentException("At least 128 bytes are required from startIndex.", nameof(value))
                : ToUInt1024(value.AsSpan(startIndex, ByteCount1024));
    }

    /// <summary>
    /// Returns a signed 1024-bit integer converted from a byte array at a specified starting position.
    /// </summary>
    /// <param name="value">The input byte array.</param>
    /// <param name="startIndex">The starting position within <paramref name="value"/>.</param>
    /// <returns>
    /// The signed 1024-bit integer represented by 128 bytes at <paramref name="startIndex"/>, interpreted in machine
    /// endianness.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="value"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="startIndex"/> is outside the bounds of <paramref name="value"/>.
    /// </exception>
    /// <exception cref="ArgumentException">
    /// Fewer than 128 bytes remain starting at <paramref name="startIndex"/>.
    /// </exception>
    public static Int1024 ToInt1024(byte[] value, int startIndex)
    {
        ArgumentNullException.ThrowIfNull(value);

        return (uint)startIndex >= (uint)value.Length
            ? throw new ArgumentOutOfRangeException(nameof(startIndex))
            : value.Length - startIndex < ByteCount1024
                ? throw new ArgumentException("At least 128 bytes are required from startIndex.", nameof(value))
                : ToInt1024(value.AsSpan(startIndex, ByteCount1024));
    }

    /// <summary>
    /// Returns an unsigned 1024-bit integer converted from a read-only byte span.
    /// </summary>
    /// <param name="value">The source bytes containing the encoded value.</param>
    /// <returns>
    /// The unsigned 1024-bit integer represented by the first 128 bytes of <paramref name="value"/>, interpreted in
    /// machine endianness.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="value"/> contains fewer than 128 bytes.</exception>
    public static UInt1024 ToUInt1024(ReadOnlySpan<byte> value)
    {
        if (value.Length < ByteCount1024)
            throw new ArgumentOutOfRangeException(nameof(value), "At least 128 bytes are required.");

        if (IsLittleEndian)
            return (UInt1024)new BigInteger(value[..ByteCount1024], isUnsigned: true, isBigEndian: false);

        Span<byte> little = stackalloc byte[ByteCount1024];
        value[..ByteCount1024].CopyTo(little);
        little.Reverse();
        return (UInt1024)new BigInteger(little, isUnsigned: true, isBigEndian: false);
    }

    /// <summary>
    /// Returns a signed 1024-bit integer converted from a read-only byte span.
    /// </summary>
    /// <param name="value">The source bytes containing the encoded value.</param>
    /// <returns>
    /// The signed 1024-bit integer represented by the first 128 bytes of <paramref name="value"/>, interpreted in
    /// machine endianness.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="value"/> contains fewer than 128 bytes.</exception>
    public static Int1024 ToInt1024(ReadOnlySpan<byte> value)
    {
        if (value.Length < ByteCount1024)
            throw new ArgumentOutOfRangeException(nameof(value), "At least 128 bytes are required.");

        if (IsLittleEndian)
            return (Int1024)new BigInteger(value[..ByteCount1024], isUnsigned: false, isBigEndian: false);

        Span<byte> little = stackalloc byte[ByteCount1024];
        value[..ByteCount1024].CopyTo(little);
        little.Reverse();
        return (Int1024)new BigInteger(little, isUnsigned: false, isBigEndian: false);
    }

    /// <summary>
    /// Writes an unsigned 256-bit value into the destination span using machine endianness.
    /// </summary>
    /// <param name="value">The value to encode.</param>
    /// <param name="destination">
    /// The destination span that must provide at least 32 bytes. Only the first 32 bytes are written.
    /// </param>
    private static void WriteUInt256EndianAware(UInt256 value, Span<byte> destination)
    {
        Span<byte> little = destination[..ByteCount256];
        little.Clear();

        byte[] src = ((BigInteger)value).ToByteArray(isUnsigned: true, isBigEndian: false);
        src.AsSpan().CopyTo(little);

        if (!IsLittleEndian)
            little.Reverse();
    }

    /// <summary>
    /// Writes a signed 256-bit value into the destination span using machine endianness.
    /// </summary>
    /// <param name="value">The value to encode.</param>
    /// <param name="destination">
    /// The destination span that must provide at least 32 bytes. Only the first 32 bytes are written.
    /// </param>
    private static void WriteInt256EndianAware(Int256 value, Span<byte> destination)
    {
        Span<byte> little = destination[..ByteCount256];
        little.Clear();

        byte[] source = ((BigInteger)value).ToByteArray(isUnsigned: false, isBigEndian: false);
        source.AsSpan(0, Math.Min(source.Length, ByteCount256)).CopyTo(little);

        if ((BigInteger)value < 0 && source.Length < ByteCount256)
            little[source.Length..].Fill(0xFF);

        if (!IsLittleEndian)
            little.Reverse();
    }

    /// <summary>
    /// Writes an unsigned 512-bit value into the destination span using machine endianness.
    /// </summary>
    /// <param name="value">The value to encode.</param>
    /// <param name="destination">
    /// The destination span that must provide at least 64 bytes. Only the first 64 bytes are written.
    /// </param>
    private static void WriteUInt512EndianAware(UInt512 value, Span<byte> destination)
    {
        Span<byte> little = destination[..ByteCount512];
        little.Clear();

        byte[] src = ((BigInteger)value).ToByteArray(isUnsigned: true, isBigEndian: false);
        src.AsSpan().CopyTo(little);

        if (!IsLittleEndian)
            little.Reverse();
    }

    /// <summary>
    /// Writes a signed 512-bit value into the destination span using machine endianness.
    /// </summary>
    /// <param name="value">The value to encode.</param>
    /// <param name="destination">
    /// The destination span that must provide at least 64 bytes. Only the first 64 bytes are written.
    /// </param>
    private static void WriteInt512EndianAware(Int512 value, Span<byte> destination)
    {
        Span<byte> little = destination[..ByteCount512];
        little.Clear();

        byte[] source = ((BigInteger)value).ToByteArray(isUnsigned: false, isBigEndian: false);
        source.AsSpan(0, Math.Min(source.Length, ByteCount512)).CopyTo(little);

        if ((BigInteger)value < 0 && source.Length < ByteCount512)
            little[source.Length..].Fill(0xFF);

        if (!IsLittleEndian)
            little.Reverse();
    }

    /// <summary>
    /// Writes an unsigned 1024-bit value into the destination span using machine endianness.
    /// </summary>
    /// <param name="value">The value to encode.</param>
    /// <param name="destination">
    /// The destination span that must provide at least 128 bytes. Only the first 128 bytes are written.
    /// </param>
    private static void WriteUInt1024EndianAware(UInt1024 value, Span<byte> destination)
    {
        Span<byte> little = destination[..ByteCount1024];
        little.Clear();

        byte[] src = ((BigInteger)value).ToByteArray(isUnsigned: true, isBigEndian: false);
        src.AsSpan().CopyTo(little);

        if (!IsLittleEndian)
            little.Reverse();
    }

    /// <summary>
    /// Writes a signed 1024-bit value into the destination span using machine endianness.
    /// </summary>
    /// <param name="value">The value to encode.</param>
    /// <param name="destination">
    /// The destination span that must provide at least 128 bytes. Only the first 128 bytes are written.
    /// </param>
    private static void WriteInt1024EndianAware(Int1024 value, Span<byte> destination)
    {
        Span<byte> little = destination[..ByteCount1024];
        little.Clear();

        byte[] source = ((BigInteger)value).ToByteArray(isUnsigned: false, isBigEndian: false);
        source.AsSpan(0, Math.Min(source.Length, ByteCount1024)).CopyTo(little);

        if ((BigInteger)value < 0 && source.Length < ByteCount1024)
            little[source.Length..].Fill(0xFF);

        if (!IsLittleEndian)
            little.Reverse();
    }

}
