// SPDX-License-Identifier: MIT

using System.Buffers;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Text;

namespace Durable7.Numerics;

/// <summary>Shared helpers for standard fixed-width integer formatting.</summary>
internal static class NumericFormatHelpers
{
    private const ulong DecimalChunkBase = 10_000_000_000_000_000_000UL;
    private static readonly ConditionalWeakTable<NumberFormatInfo, int[]> s_numberGroupSizes = new();

    public static bool TryFormatUnsigned(
        ReadOnlySpan<ulong> magnitude,
        bool negative,
        Span<char> destination,
        out int charsWritten,
        ReadOnlySpan<char> format,
        IFormatProvider? provider)
    {
        char specifier = format.IsEmpty ? 'G' : format[0];
        int precision = ParsePrecision(format);
        switch (specifier)
        {
            case 'G':
            case 'g':
                if (precision >= 0)
                    throw new FormatException();
                return TryFormatDecimal(magnitude, negative, destination, out charsWritten, 0, false, provider);
            case 'D':
            case 'd':
                return TryFormatDecimal(magnitude, negative, destination, out charsWritten,
                    precision < 0 ? 0 : precision, false, provider);
            case 'N':
            case 'n':
                return TryFormatDecimal(magnitude, negative, destination, out charsWritten,
                    precision, true, provider);
            case 'X':
            case 'x':
                return TryFormatHex(magnitude, destination, out charsWritten,
                    precision < 0 ? 0 : precision, specifier == 'x');
            default:
                throw new FormatException();
        }
    }

    public static bool TryFormatUnsignedUtf8(
        ReadOnlySpan<ulong> magnitude,
        bool negative,
        Span<byte> destination,
        out int bytesWritten,
        ReadOnlySpan<char> format,
        IFormatProvider? provider)
    {
        char[]? rented = null;
        int capacity = Math.Max(1, destination.Length);
        Span<char> characters = capacity <= 512
            ? stackalloc char[capacity]
            : (rented = ArrayPool<char>.Shared.Rent(capacity));
        try
        {
            if (!TryFormatUnsigned(magnitude, negative, characters, out int charsWritten, format, provider))
            {
                bytesWritten = 0;
                return false;
            }

            return Encoding.UTF8.TryGetBytes(characters[..charsWritten], destination, out bytesWritten);
        }
        finally
        {
            if (rented is not null)
                ArrayPool<char>.Shared.Return(rented);
        }
    }

    /// <summary>Applies the precision component of a hexadecimal standard format string.</summary>
    /// <param name="digits">The unpadded hexadecimal digits.</param>
    /// <param name="format">The complete standard format string.</param>
    /// <returns><paramref name="digits"/> padded with leading zeroes to the requested minimum width.</returns>
    /// <exception cref="FormatException">The precision component is not a non-negative decimal integer.</exception>
    public static string ApplyHexPrecision(string digits, string format)
    {
        if (format.Length == 1)
            return digits;

        if (!int.TryParse(format.AsSpan(1), NumberStyles.None, CultureInfo.InvariantCulture, out int precision))
            throw new FormatException();

        return precision > digits.Length ? digits.PadLeft(precision, '0') : digits;
    }

    private static int ParsePrecision(ReadOnlySpan<char> format)
    {
        if (format.Length <= 1)
            return -1;

        int precision = 0;
        foreach (char c in format[1..])
        {
            int digit = c - '0';
            if ((uint)digit > 9 || precision > (int.MaxValue - digit) / 10)
                throw new FormatException();
            precision = precision * 10 + digit;
        }
        return precision;
    }

    private static bool TryFormatDecimal(
        ReadOnlySpan<ulong> magnitude,
        bool negative,
        Span<char> destination,
        out int charsWritten,
        int precision,
        bool number,
        IFormatProvider? provider)
    {
        Span<ulong> working = stackalloc ulong[magnitude.Length];
        magnitude.CopyTo(working);
        Span<ulong> chunks = stackalloc ulong[18];
        int chunkCount = 0;
        while (!IsZero(working))
            chunks[chunkCount++] = WideIntegerAlgorithms.DivideInPlace(working, DecimalChunkBase);

        Span<char> digits = stackalloc char[309];
        int digitCount = 0;
        if (chunkCount == 0)
        {
            digits[digitCount++] = '0';
        }
        else
        {
            WriteDecimalChunk(chunks[chunkCount - 1], digits, ref digitCount, 0);
            for (int i = chunkCount - 2; i >= 0; i--)
                WriteDecimalChunk(chunks[i], digits, ref digitCount, 19);
        }

        NumberFormatInfo nfi = NumberFormatInfo.GetInstance(provider);
        string sign = negative ? nfi.NegativeSign : string.Empty;
        if (!number)
        {
            int integerLength = Math.Max(digitCount, precision);
            long required = (long)sign.Length + integerLength;
            if (required > destination.Length)
            {
                charsWritten = 0;
                return false;
            }

            sign.AsSpan().CopyTo(destination);
            int offset = sign.Length;
            destination.Slice(offset, integerLength - digitCount).Fill('0');
            digits[..digitCount].CopyTo(destination[(offset + integerLength - digitCount)..]);
            charsWritten = (int)required;
            return true;
        }

        // The "N" format lays the negative value out according to NumberNegativePattern, matching the
        // BCL (and this library's own string ToString path, which delegates to BigInteger).
        string prefix = string.Empty;
        string suffix = string.Empty;
        if (negative)
        {
            (prefix, suffix) = nfi.NumberNegativePattern switch
            {
                0 => ("(", ")"),
                2 => (nfi.NegativeSign + " ", string.Empty),
                3 => (string.Empty, nfi.NegativeSign),
                4 => (string.Empty, " " + nfi.NegativeSign),
                _ => (nfi.NegativeSign, string.Empty),
            };
        }

        int decimalDigits = precision < 0 ? nfi.NumberDecimalDigits : precision;
        int[] groupSizes = nfi.IsReadOnly
            ? s_numberGroupSizes.GetValue(nfi, static value => value.NumberGroupSizes)
            : nfi.NumberGroupSizes;
        Span<int> groups = stackalloc int[310];
        int groupCount = BuildGroups(digitCount, groupSizes, groups);
        string groupSeparator = nfi.NumberGroupSeparator;
        string decimalSeparator = nfi.NumberDecimalSeparator;
        long requiredNumber = (long)prefix.Length + digitCount + (long)(groupCount - 1) * groupSeparator.Length
            + suffix.Length;
        if (decimalDigits != 0)
            requiredNumber += decimalSeparator.Length + decimalDigits;
        if (requiredNumber > destination.Length)
        {
            charsWritten = 0;
            return false;
        }

        prefix.AsSpan().CopyTo(destination);
        int write = prefix.Length;
        int digitOffset = 0;
        for (int group = groupCount - 1; group >= 0; group--)
        {
            int length = groups[group];
            digits.Slice(digitOffset, length).CopyTo(destination[write..]);
            digitOffset += length;
            write += length;
            if (group != 0)
            {
                groupSeparator.AsSpan().CopyTo(destination[write..]);
                write += groupSeparator.Length;
            }
        }
        if (decimalDigits != 0)
        {
            decimalSeparator.AsSpan().CopyTo(destination[write..]);
            write += decimalSeparator.Length;
            destination.Slice(write, decimalDigits).Fill('0');
            write += decimalDigits;
        }
        suffix.AsSpan().CopyTo(destination[write..]);
        write += suffix.Length;

        charsWritten = write;
        return true;
    }

    private static bool TryFormatHex(
        ReadOnlySpan<ulong> magnitude,
        Span<char> destination,
        out int charsWritten,
        int precision,
        bool lower)
    {
        Span<char> digits = stackalloc char[256];
        int count = 0;
        bool started = false;
        char letterBase = lower ? 'a' : 'A';
        for (int limb = magnitude.Length - 1; limb >= 0; limb--)
        {
            for (int shift = 60; shift >= 0; shift -= 4)
            {
                int nibble = (int)((magnitude[limb] >> shift) & 0xFUL);
                if (!started && nibble == 0)
                    continue;
                started = true;
                digits[count++] = (char)(nibble < 10 ? '0' + nibble : letterBase + nibble - 10);
            }
        }
        if (!started)
            digits[count++] = '0';

        int required = Math.Max(count, precision);
        if (required > destination.Length)
        {
            charsWritten = 0;
            return false;
        }
        destination[..(required - count)].Fill('0');
        digits[..count].CopyTo(destination[(required - count)..]);
        charsWritten = required;
        return true;
    }

    private static bool IsZero(ReadOnlySpan<ulong> value)
    {
        foreach (ulong limb in value)
            if (limb != 0)
                return false;
        return true;
    }

    private static void WriteDecimalChunk(ulong value, Span<char> destination, ref int offset, int width)
    {
        int digits = 1;
        for (ulong remaining = value; remaining >= 10; remaining /= 10)
            digits++;
        int total = Math.Max(width, digits);
        int end = offset + total;
        for (int i = end - 1; i >= offset; i--)
        {
            destination[i] = (char)('0' + value % 10);
            value /= 10;
        }
        offset = end;
    }

    private static int BuildGroups(int digitCount, ReadOnlySpan<int> groupSizes, Span<int> groups)
    {
        if (groupSizes.IsEmpty || groupSizes[0] <= 0)
        {
            groups[0] = digitCount;
            return 1;
        }

        int remaining = digitCount;
        int count = 0;
        int sizeIndex = 0;
        int groupSize = groupSizes[0];
        while (groupSize > 0 && remaining > groupSize)
        {
            groups[count++] = groupSize;
            remaining -= groupSize;
            if (sizeIndex + 1 < groupSizes.Length && groupSizes[sizeIndex + 1] != 0)
                groupSize = groupSizes[++sizeIndex];
            else if (sizeIndex + 1 < groupSizes.Length && groupSizes[sizeIndex + 1] == 0)
                groupSize = 0;
        }
        groups[count++] = remaining;
        return count;
    }
}
