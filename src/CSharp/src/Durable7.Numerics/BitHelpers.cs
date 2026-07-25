// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Numerics;
using System.Runtime.CompilerServices;

namespace Durable7.Numerics;

/// <summary>
/// Provides low-level bit-manipulation and endian-conversion helpers shared by fixed-width integer types.
/// </summary>
/// <remarks>
/// <para>
/// These helpers are intentionally narrow and allocation-free. They assume fixed-width buffers (16 bytes for
/// <see cref="UInt128"/>, 32 bytes for <see cref="UInt256"/>, and 64 bytes for <see cref="UInt512"/>) where required
/// and are used internally as building blocks for the wider numeric types.
/// </para>
/// <para>
/// Although these methods are internal, they define important invariants for the entire integer stack: byte-order
/// helpers always read or write fixed-width little-endian buffers, and bit-count helpers follow the same boundary
/// behavior as <see cref="BitOperations"/> primitives.
/// </para>
/// </remarks>
internal static class BitHelpers
{
    /// <summary>
    /// Counts the number of leading zero bits in a 128-bit unsigned integer.
    /// </summary>
    /// <param name="value">The value to inspect.</param>
    /// <returns>
    /// A value in the range <c>0</c> to <c>128</c>, where <c>128</c> is returned when <paramref name="value"/>
    /// is zero.
    /// </returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int LeadingZeroCount(UInt128 value)
    {
        if (value == 0)
            return 128;

        ulong hi = (ulong)(value >> 64);
        return hi == 0
            ? 64 + BitOperations.LeadingZeroCount((ulong)value)
            : BitOperations.LeadingZeroCount(hi);
    }

    /// <summary>
    /// Counts the number of trailing zero bits in a 128-bit unsigned integer.
    /// </summary>
    /// <param name="value">The value to inspect.</param>
    /// <returns>
    /// A value in the range <c>0</c> to <c>128</c>, where <c>128</c> is returned when <paramref name="value"/>
    /// is zero.
    /// </returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int TrailingZeroCount(UInt128 value)
    {
        if (value == 0)
            return 128;

        ulong lo = (ulong)value;
        return lo == 0
            ? 64 + BitOperations.TrailingZeroCount((ulong)(value >> 64))
            : BitOperations.TrailingZeroCount(lo);
    }

    /// <summary>
    /// Counts the number of set bits in a 128-bit unsigned integer.
    /// </summary>
    /// <param name="value">The value to inspect.</param>
    /// <returns>The number of bits set to <c>1</c> in <paramref name="value"/>.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int PopCount(UInt128 value) =>
        BitOperations.PopCount((ulong)value) + BitOperations.PopCount((ulong)(value >> 64));

    /// <summary>
    /// Reads a 128-bit unsigned integer from a 16-byte little-endian buffer.
    /// </summary>
    /// <param name="source16">A 16-byte span whose first byte is the least significant byte.</param>
    /// <returns>The parsed <see cref="UInt128"/> value.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="source16"/> is shorter than 16 bytes.</exception>
    /// <remarks>
    /// This routine assumes fixed-width payloads and performs no endian autodetection. Callers that ingest
    /// architecture-endian payloads should normalize first.
    /// </remarks>
    /// <example>
    /// <code>
    /// Span&lt;byte&gt; bytes = stackalloc byte[16];
    /// bytes[0] = 0x78;
    /// bytes[1] = 0x56;
    /// bytes[2] = 0x34;
    /// bytes[3] = 0x12;
    ///
    /// UInt128 value = BitHelpers.ReadUInt128LittleEndian(bytes);
    /// // value == 0x00000000000000000000000012345678
    /// </code>
    /// </example>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt128 ReadUInt128LittleEndian(ReadOnlySpan<byte> source16) =>
        ((UInt128)BinaryPrimitives.ReadUInt64LittleEndian(source16.Slice(8, 8)) << 64) |
        BinaryPrimitives.ReadUInt64LittleEndian(source16[..8]);


    /// <summary>
    /// Writes a 128-bit unsigned integer to a 16-byte little-endian destination span.
    /// </summary>
    /// <param name="value">The value to write.</param>
    /// <param name="destination16">A 16-byte destination span that receives the serialized bytes.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="destination16"/> is shorter than 16 bytes.</exception>
    /// <remarks>
    /// Only the first 16 bytes are written; callers can safely pass a larger scratch buffer and slice the written
    /// region as needed.
    /// </remarks>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void WriteUInt128LittleEndian(UInt128 value, Span<byte> destination16)
    {
        BinaryPrimitives.WriteUInt64LittleEndian(destination16[..8], (ulong)value);
        BinaryPrimitives.WriteUInt64LittleEndian(destination16.Slice(8, 8), (ulong)(value >> 64));
    }

    /// <summary>
    /// Counts the number of leading zero bits in a 256-bit unsigned integer.
    /// </summary>
    /// <param name="value">The value to inspect.</param>
    /// <returns>
    /// A value in the range <c>0</c> to <c>256</c>, where <c>256</c> is returned when <paramref name="value"/>
    /// is zero.
    /// </returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int LeadingZeroCount(UInt256 value) =>
        value.Upper == 0
            ? 128 + LeadingZeroCount(value.Lower)
            : LeadingZeroCount(value.Upper);

    /// <summary>
    /// Counts the number of trailing zero bits in a 256-bit unsigned integer.
    /// </summary>
    /// <param name="value">The value to inspect.</param>
    /// <returns>
    /// A value in the range <c>0</c> to <c>256</c>, where <c>256</c> is returned when <paramref name="value"/>
    /// is zero.
    /// </returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int TrailingZeroCount(UInt256 value) =>
        value.Lower == 0
            ? 128 + TrailingZeroCount(value.Upper)
            : TrailingZeroCount(value.Lower);

    /// <summary>
    /// Counts the number of set bits in a 256-bit unsigned integer.
    /// </summary>
    /// <param name="value">The value to inspect.</param>
    /// <returns>The number of bits set to <c>1</c> in <paramref name="value"/>.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int PopCount(UInt256 value) =>
        PopCount(value.Lower) + PopCount(value.Upper);

    /// <summary>
    /// Reads a 256-bit unsigned integer from a 32-byte little-endian buffer.
    /// </summary>
    /// <param name="source32">A 32-byte span whose first byte is the least significant byte.</param>
    /// <returns>The parsed <see cref="UInt256"/> value.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="source32"/> is shorter than 32 bytes.</exception>
    /// <example>
    /// <code>
    /// Span&lt;byte&gt; payload = stackalloc byte[32];
    /// BitHelpers.WriteUInt256LittleEndian(UInt256.One, payload);
    /// UInt256 roundTrip = BitHelpers.ReadUInt256LittleEndian(payload);
    /// // roundTrip == UInt256.One
    /// </code>
    /// </example>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt256 ReadUInt256LittleEndian(ReadOnlySpan<byte> source32) =>
        new(
            ReadUInt128LittleEndian(source32.Slice(16, 16)),
            ReadUInt128LittleEndian(source32[..16]));

    /// <summary>
    /// Writes a 256-bit unsigned integer to a 32-byte little-endian destination span.
    /// </summary>
    /// <param name="value">The value to write.</param>
    /// <param name="destination32">A 32-byte destination span that receives the serialized bytes.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="destination32"/> is shorter than 32 bytes.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void WriteUInt256LittleEndian(UInt256 value, Span<byte> destination32)
    {
        WriteUInt128LittleEndian(value.Lower, destination32[..16]);
        WriteUInt128LittleEndian(value.Upper, destination32.Slice(16, 16));
    }

    /// <summary>
    /// Counts the number of leading zero bits in a 512-bit unsigned integer.
    /// </summary>
    /// <param name="value">The value to inspect.</param>
    /// <returns>
    /// A value in the range <c>0</c> to <c>512</c>, where <c>512</c> is returned when <paramref name="value"/>
    /// is zero.
    /// </returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int LeadingZeroCount(UInt512 value) =>
        value.Upper == 0
            ? 256 + LeadingZeroCount(value.Lower)
            : LeadingZeroCount(value.Upper);

    /// <summary>
    /// Counts the number of trailing zero bits in a 512-bit unsigned integer.
    /// </summary>
    /// <param name="value">The value to inspect.</param>
    /// <returns>
    /// A value in the range <c>0</c> to <c>512</c>, where <c>512</c> is returned when <paramref name="value"/>
    /// is zero.
    /// </returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int TrailingZeroCount(UInt512 value) =>
        value.Lower == 0
            ? 256 + TrailingZeroCount(value.Upper)
            : TrailingZeroCount(value.Lower);

    /// <summary>
    /// Counts the number of set bits in a 512-bit unsigned integer.
    /// </summary>
    /// <param name="value">The value to inspect.</param>
    /// <returns>The number of bits set to <c>1</c> in <paramref name="value"/>.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int PopCount(UInt512 value) =>
        PopCount(value.Lower) + PopCount(value.Upper);

    /// <summary>
    /// Reads a 512-bit unsigned integer from a 64-byte little-endian buffer.
    /// </summary>
    /// <param name="source64">A 64-byte span whose first byte is the least significant byte.</param>
    /// <returns>The parsed <see cref="UInt512"/> value.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="source64"/> is shorter than 64 bytes.</exception>
    /// <remarks>
    /// The method composes two 256-bit segments and preserves the same lower-then-upper ordering used by the
    /// <see cref="UInt512"/> constructor.
    /// </remarks>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static UInt512 ReadUInt512LittleEndian(ReadOnlySpan<byte> source64) =>
        new(
            ReadUInt256LittleEndian(source64.Slice(32, 32)),
            ReadUInt256LittleEndian(source64[..32]));

    /// <summary>
    /// Writes a 512-bit unsigned integer to a 64-byte little-endian destination span.
    /// </summary>
    /// <param name="value">The value to write.</param>
    /// <param name="destination64">A 64-byte destination span that receives the serialized bytes.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="destination64"/> is shorter than 64 bytes.</exception>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void WriteUInt512LittleEndian(UInt512 value, Span<byte> destination64)
    {
        WriteUInt256LittleEndian(value.Lower, destination64[..32]);
        WriteUInt256LittleEndian(value.Upper, destination64.Slice(32, 32));
    }
}
