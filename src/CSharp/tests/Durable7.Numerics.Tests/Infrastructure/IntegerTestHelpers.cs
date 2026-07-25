using System.Numerics;

namespace Durable7.Numerics.Tests;

/// <summary>
/// Centralizes shared numeric reference helpers used by the <c>Durable7.Numerics.Tests</c> suite.
/// </summary>
/// <remarks>
/// <para>
/// The test suite validates <see cref="UInt256"/>, <see cref="Int256"/>, <see cref="UInt512"/>, and
/// <see cref="Int512"/> by comparing each operation against mathematically equivalent
/// <see cref="BigInteger"/> computations. This helper type keeps conversion, normalization, and random-data
/// generation logic in one location so all tests use identical modeling rules.
/// </para>
/// <para>
/// The normalization members explicitly model two different domains:
/// </para>
/// <list type="bullet">
/// <item>
/// <description>Unsigned fixed-width arithmetic with modulo <c>2^N</c> wraparound.</description>
/// </item>
/// <item>
/// <description>
/// Signed fixed-width two's-complement arithmetic over the closed range <c>[-2^(N-1), 2^(N-1) - 1]</c>.
/// </description>
/// </item>
/// </list>
/// <para>
/// Keeping these conversions centralized prevents accidental divergence between test classes and makes it easier to
/// reason about failures as contract mismatches rather than mismatched test scaffolding.
/// </para>
/// </remarks>
internal static class IntegerTestHelpers
{
    /// <summary>
    /// Gets the largest value representable by <see cref="UInt512"/>.
    /// </summary>
    /// <remarks>
    /// The value is mathematically equal to <c>2^512 - 1</c>.
    /// </remarks>
    public static readonly BigInteger UInt512Max = (BigInteger.One << 512) - BigInteger.One;

    /// <summary>
    /// Gets the smallest value representable by <see cref="Int512"/>.
    /// </summary>
    /// <remarks>
    /// The value is mathematically equal to <c>-2^511</c>.
    /// </remarks>
    public static readonly BigInteger Int512Min = -(BigInteger.One << 511);

    /// <summary>
    /// Gets the largest value representable by <see cref="Int512"/>.
    /// </summary>
    /// <remarks>
    /// The value is mathematically equal to <c>2^511 - 1</c>.
    /// </remarks>
    public static readonly BigInteger Int512Max = (BigInteger.One << 511) - BigInteger.One;



    /// <summary>
    /// Gets the largest value representable by <see cref="UInt1024"/>.
    /// </summary>
    /// <remarks>
    /// The value is mathematically equal to <c>2^1024 - 1</c>.
    /// </remarks>
    public static readonly BigInteger UInt1024Max = (BigInteger.One << 1024) - BigInteger.One;

    /// <summary>
    /// Gets the smallest value representable by <see cref="Int1024"/>.
    /// </summary>
    /// <remarks>
    /// The value is mathematically equal to <c>-2^1023</c>.
    /// </remarks>
    public static readonly BigInteger Int1024Min = -(BigInteger.One << 1023);

    /// <summary>
    /// Gets the largest value representable by <see cref="Int1024"/>.
    /// </summary>
    /// <remarks>
    /// The value is mathematically equal to <c>2^1023 - 1</c>.
    /// </remarks>
    public static readonly BigInteger Int1024Max = (BigInteger.One << 1023) - BigInteger.One;
    /// <summary>
    /// Gets the largest value representable by <see cref="UInt256"/>.
    /// </summary>
    /// <remarks>
    /// The value is mathematically equal to <c>2^256 - 1</c>.
    /// </remarks>
    public static readonly BigInteger UIntMax = (BigInteger.One << 256) - BigInteger.One;

    /// <summary>
    /// Gets the smallest value representable by <see cref="Int256"/>.
    /// </summary>
    /// <remarks>
    /// The value is mathematically equal to <c>-2^255</c>.
    /// </remarks>
    public static readonly BigInteger IntMin = -(BigInteger.One << 255);

    /// <summary>
    /// Gets the largest value representable by <see cref="Int256"/>.
    /// </summary>
    /// <remarks>
    /// The value is mathematically equal to <c>2^255 - 1</c>.
    /// </remarks>
    public static readonly BigInteger IntMax = (BigInteger.One << 255) - BigInteger.One;

    /// <summary>
    /// Converts a <see cref="BigInteger"/> reference value into a <see cref="UInt256"/> operand.
    /// </summary>
    /// <param name="value">The source value to convert.</param>
    /// <returns>
    /// A <see cref="UInt256"/> produced by the type conversion semantics (in-range values are preserved;
    /// out-of-range values follow the conversion's wrap/overflow rules).
    /// </returns>
    public static UInt256 UIntFromBigInteger(BigInteger value) => (UInt256)value;

    /// <summary>
    /// Normalizes a value into the canonical unsigned 256-bit domain.
    /// </summary>
    /// <param name="value">The input value to normalize.</param>
    /// <returns>
    /// A value in the inclusive range <c>[0, 2^256 - 1]</c>, computed via modulo <c>2^256</c> with a non-negative
    /// result.
    /// </returns>
    /// <remarks>
    /// This mirrors wraparound behavior for unchecked <see cref="UInt256"/> arithmetic.
    /// </remarks>
    public static BigInteger NormalizeUnsigned(BigInteger value)
    {
        var modulus = BigInteger.One << 256;
        var normalized = value % modulus;
        return normalized.Sign < 0 ? normalized + modulus : normalized;
    }

    /// <summary>
    /// Normalizes a value into the canonical signed 256-bit two's-complement domain.
    /// </summary>
    /// <param name="value">The input value to normalize.</param>
    /// <returns>
    /// A value in the inclusive range <c>[-2^255, 2^255 - 1]</c> representing the same 256-bit bit pattern.
    /// </returns>
    /// <remarks>
    /// The method first normalizes into the unsigned domain and then reinterprets values greater than or equal to
    /// <c>2^255</c> as negative two's-complement values.
    /// </remarks>
    public static BigInteger NormalizeSigned(BigInteger value)
    {
        var normalized = NormalizeUnsigned(value);
        return normalized >= BigInteger.One << 255
            ? normalized - (BigInteger.One << 256)
            : normalized;
    }

    /// <summary>
    /// Creates a pseudo-random <see cref="UInt256"/> sample using 32 random bytes.
    /// </summary>
    /// <param name="random">The deterministic random source configured by the calling test.</param>
    /// <returns>A value whose underlying bits come directly from <paramref name="random"/>.</returns>
    /// <remarks>
    /// Tests always pass a seeded <see cref="Random"/> instance to keep generated datasets reproducible across runs.
    /// </remarks>
    public static UInt256 RandomUInt256(Random random)
    {
        Span<byte> bytes = stackalloc byte[32];
        random.NextBytes(bytes);
        return (UInt256)new BigInteger(bytes, isUnsigned: true, isBigEndian: false);
    }

    /// <summary>
    /// Creates a pseudo-random <see cref="Int256"/> sample using 32 random bytes.
    /// </summary>
    /// <param name="random">The deterministic random source configured by the calling test.</param>
    /// <returns>
    /// A value whose underlying bits come directly from <paramref name="random"/>, interpreted as signed
    /// two's-complement.
    /// </returns>
    /// <remarks>
    /// Tests always pass a seeded <see cref="Random"/> instance to keep generated datasets reproducible across runs.
    /// </remarks>
    public static Int256 RandomInt256(Random random)
    {
        Span<byte> bytes = stackalloc byte[32];
        random.NextBytes(bytes);
        return (Int256)new BigInteger(bytes, isUnsigned: false, isBigEndian: false);
    }

    /// <summary>
    /// Converts a <see cref="BigInteger"/> reference value into a <see cref="UInt512"/> operand.
    /// </summary>
    /// <param name="value">The source value to convert.</param>
    /// <returns>
    /// A <see cref="UInt512"/> produced by the type conversion semantics (in-range values are preserved;
    /// out-of-range values follow the conversion's wrap/overflow rules).
    /// </returns>
    public static UInt512 UInt512FromBigInteger(BigInteger value) => (UInt512)value;

    /// <summary>
    /// Normalizes a value into the canonical unsigned 512-bit domain.
    /// </summary>
    /// <param name="value">The input value to normalize.</param>
    /// <returns>
    /// A value in the inclusive range <c>[0, 2^512 - 1]</c>, computed via modulo <c>2^512</c> with a non-negative
    /// result.
    /// </returns>
    public static BigInteger NormalizeUnsigned512(BigInteger value)
    {
        var modulus = BigInteger.One << 512;
        var normalized = value % modulus;
        return normalized.Sign < 0 ? normalized + modulus : normalized;
    }

    /// <summary>
    /// Normalizes a value into the canonical signed 512-bit two's-complement domain.
    /// </summary>
    /// <param name="value">The input value to normalize.</param>
    /// <returns>
    /// A value in the inclusive range <c>[-2^511, 2^511 - 1]</c> representing the same 512-bit bit pattern.
    /// </returns>
    public static BigInteger NormalizeSigned512(BigInteger value)
    {
        var normalized = NormalizeUnsigned512(value);
        return normalized >= BigInteger.One << 511
            ? normalized - (BigInteger.One << 512)
            : normalized;
    }

    /// <summary>
    /// Creates a pseudo-random <see cref="UInt512"/> sample using 64 random bytes.
    /// </summary>
    /// <param name="random">The deterministic random source configured by the calling test.</param>
    /// <returns>A value whose underlying bits come directly from <paramref name="random"/>.</returns>
    public static UInt512 RandomUInt512(Random random)
    {
        Span<byte> bytes = stackalloc byte[64];
        random.NextBytes(bytes);
        return (UInt512)new BigInteger(bytes, isUnsigned: true, isBigEndian: false);
    }

    /// <summary>
    /// Creates a pseudo-random <see cref="Int512"/> sample using 64 random bytes.
    /// </summary>
    /// <param name="random">The deterministic random source configured by the calling test.</param>
    /// <returns>
    /// A value whose underlying bits come directly from <paramref name="random"/>, interpreted as signed
    /// two's-complement.
    /// </returns>
    public static Int512 RandomInt512(Random random)
    {
        Span<byte> bytes = stackalloc byte[64];
        random.NextBytes(bytes);
        return (Int512)new BigInteger(bytes, isUnsigned: false, isBigEndian: false);
    }


    /// <summary>
    /// Converts a <see cref="BigInteger"/> reference value into a <see cref="UInt1024"/> operand.
    /// </summary>
    /// <param name="value">The source value to convert.</param>
    /// <returns>
    /// A <see cref="UInt1024"/> produced by the type conversion semantics (in-range values are preserved;
    /// out-of-range values follow the conversion's wrap/overflow rules).
    /// </returns>
    public static UInt1024 UInt1024FromBigInteger(BigInteger value) => (UInt1024)value;

    /// <summary>
    /// Normalizes a value into the canonical unsigned 1024-bit domain.
    /// </summary>
    /// <param name="value">The input value to normalize.</param>
    /// <returns>
    /// A value in the inclusive range <c>[0, 2^1024 - 1]</c>, computed via modulo <c>2^1024</c> with a non-negative
    /// result.
    /// </returns>
    public static BigInteger NormalizeUnsigned1024(BigInteger value)
    {
        var modulus = BigInteger.One << 1024;
        var normalized = value % modulus;
        return normalized.Sign < 0 ? normalized + modulus : normalized;
    }

    /// <summary>
    /// Normalizes a value into the canonical signed 1024-bit two's-complement domain.
    /// </summary>
    /// <param name="value">The input value to normalize.</param>
    /// <returns>
    /// A value in the inclusive range <c>[-2^1023, 2^1023 - 1]</c> representing the same 1024-bit bit pattern.
    /// </returns>
    public static BigInteger NormalizeSigned1024(BigInteger value)
    {
        var normalized = NormalizeUnsigned1024(value);
        return normalized >= BigInteger.One << 1023
            ? normalized - (BigInteger.One << 1024)
            : normalized;
    }

    /// <summary>
    /// Creates a pseudo-random <see cref="UInt1024"/> sample using 128 random bytes.
    /// </summary>
    /// <param name="random">The deterministic random source configured by the calling test.</param>
    /// <returns>A value whose underlying bits come directly from <paramref name="random"/>.</returns>
    public static UInt1024 RandomUInt1024(Random random)
    {
        Span<byte> bytes = stackalloc byte[128];
        random.NextBytes(bytes);
        return (UInt1024)new BigInteger(bytes, isUnsigned: true, isBigEndian: false);
    }

    /// <summary>
    /// Creates a pseudo-random <see cref="Int1024"/> sample using 128 random bytes.
    /// </summary>
    /// <param name="random">The deterministic random source configured by the calling test.</param>
    /// <returns>
    /// A value whose underlying bits come directly from <paramref name="random"/>, interpreted as signed
    /// two's-complement.
    /// </returns>
    public static Int1024 RandomInt1024(Random random)
    {
        Span<byte> bytes = stackalloc byte[128];
        random.NextBytes(bytes);
        return (Int1024)new BigInteger(bytes, isUnsigned: false, isBigEndian: false);
    }

}
