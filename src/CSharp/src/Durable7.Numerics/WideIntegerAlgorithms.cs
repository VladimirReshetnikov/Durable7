// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Durable7.Numerics;

/// <summary>Allocation-free limb algorithms shared by the fixed-width unsigned integers.</summary>
internal static class WideIntegerAlgorithms
{
    public static void WriteLimbs(UInt256 value, Span<ulong> limbs)
    {
        limbs[0] = (ulong)value.Lower;
        limbs[1] = (ulong)(value.Lower >> 64);
        limbs[2] = (ulong)value.Upper;
        limbs[3] = (ulong)(value.Upper >> 64);
    }

    public static void WriteLimbs(UInt512 value, Span<ulong> limbs)
    {
        WriteLimbs(value.Lower, limbs[..4]);
        WriteLimbs(value.Upper, limbs[4..8]);
    }

    public static void WriteLimbs(UInt1024 value, Span<ulong> limbs)
    {
        WriteLimbs(value.Lower, limbs[..8]);
        WriteLimbs(value.Upper, limbs[8..16]);
    }

    public static UInt256 CreateUInt256(ReadOnlySpan<ulong> limbs) =>
        new(((UInt128)limbs[3] << 64) | limbs[2], ((UInt128)limbs[1] << 64) | limbs[0]);

    public static UInt512 CreateUInt512(ReadOnlySpan<ulong> limbs) =>
        new(CreateUInt256(limbs[4..8]), CreateUInt256(limbs[..4]));

    public static UInt1024 CreateUInt1024(ReadOnlySpan<ulong> limbs) =>
        new(CreateUInt512(limbs[8..16]), CreateUInt512(limbs[..8]));

    /// <summary>Divides little-endian 64-bit limbs using normalized Knuth long division.</summary>
    public static void DivRem(
        ReadOnlySpan<ulong> dividend,
        ReadOnlySpan<ulong> divisor,
        Span<ulong> quotient,
        Span<ulong> remainder)
    {
        quotient.Clear();
        remainder.Clear();

        int dividendLength = SignificantLength(dividend);
        int divisorLength = SignificantLength(divisor);
        if (divisorLength == 0)
            throw new DivideByZeroException();

        if (dividendLength == 0)
            return;

        if (Compare(dividend[..dividendLength], divisor[..divisorLength]) < 0)
        {
            dividend[..dividendLength].CopyTo(remainder);
            return;
        }

        if (divisorLength == 1)
        {
            ulong rem = 0;
            ulong divisorValue = divisor[0];
            for (int i = dividendLength - 1; i >= 0; i--)
            {
                UInt128 partial = ((UInt128)rem << 64) | dividend[i];
                quotient[i] = (ulong)(partial / divisorValue);
                rem = (ulong)(partial % divisorValue);
            }

            remainder[0] = rem;
            return;
        }

        int shift = BitOperations.LeadingZeroCount(divisor[divisorLength - 1]);
        Span<ulong> normalizedDivisor = stackalloc ulong[divisorLength];
        Span<ulong> normalizedDividend = stackalloc ulong[dividendLength + 1];
        ShiftLeft(divisor[..divisorLength], shift, normalizedDivisor);
        ShiftLeft(dividend[..dividendLength], shift, normalizedDividend);

        int quotientLength = dividendLength - divisorLength + 1;
        ulong divisorHigh = normalizedDivisor[divisorLength - 1];
        ulong divisorNext = normalizedDivisor[divisorLength - 2];
        for (int j = quotientLength - 1; j >= 0; j--)
        {
            ulong quotientDigit;
            ulong estimatedRemainder;
            bool estimateOverflowed = false;
            ulong top = normalizedDividend[j + divisorLength];
            ulong next = normalizedDividend[j + divisorLength - 1];
            if (top == divisorHigh)
            {
                quotientDigit = ulong.MaxValue;
                estimatedRemainder = unchecked(next + divisorHigh);
                estimateOverflowed = estimatedRemainder < next;
            }
            else
            {
                UInt128 numerator = ((UInt128)top << 64) | next;
                quotientDigit = (ulong)(numerator / divisorHigh);
                estimatedRemainder = (ulong)(numerator % divisorHigh);
            }

            while (!estimateOverflowed && (UInt128)quotientDigit * divisorNext >
                ((UInt128)estimatedRemainder << 64) + normalizedDividend[j + divisorLength - 2])
            {
                quotientDigit--;
                ulong previous = estimatedRemainder;
                estimatedRemainder = unchecked(estimatedRemainder + divisorHigh);
                estimateOverflowed = estimatedRemainder < previous;
            }

            Span<ulong> window = normalizedDividend.Slice(j, divisorLength + 1);
            if (SubtractProduct(window, normalizedDivisor, quotientDigit))
            {
                quotientDigit--;
                AddBack(window, normalizedDivisor);
            }

            quotient[j] = quotientDigit;
        }

        ShiftRight(normalizedDividend[..divisorLength], shift, remainder);
    }

    /// <summary>Divides a mutable little-endian magnitude by one 64-bit limb.</summary>
    public static ulong DivideInPlace(Span<ulong> value, ulong divisor)
    {
        ulong remainder = 0;
        for (int i = value.Length - 1; i >= 0; i--)
        {
            UInt128 partial = ((UInt128)remainder << 64) | value[i];
            value[i] = (ulong)(partial / divisor);
            remainder = (ulong)(partial % divisor);
        }

        return remainder;
    }

    private static int SignificantLength(ReadOnlySpan<ulong> value)
    {
        int length = value.Length;
        while (length != 0 && value[length - 1] == 0)
            length--;
        return length;
    }

    private static int Compare(ReadOnlySpan<ulong> left, ReadOnlySpan<ulong> right)
    {
        if (left.Length != right.Length)
            return left.Length.CompareTo(right.Length);
        for (int i = left.Length - 1; i >= 0; i--)
        {
            int comparison = left[i].CompareTo(right[i]);
            if (comparison != 0)
                return comparison;
        }
        return 0;
    }

    private static void ShiftLeft(ReadOnlySpan<ulong> source, int shift, Span<ulong> destination)
    {
        destination.Clear();
        if (shift == 0)
        {
            source.CopyTo(destination);
            return;
        }

        ulong carry = 0;
        for (int i = 0; i < source.Length; i++)
        {
            ulong current = source[i];
            destination[i] = (current << shift) | carry;
            carry = current >> (64 - shift);
        }
        if (destination.Length > source.Length)
            destination[source.Length] = carry;
    }

    private static void ShiftRight(ReadOnlySpan<ulong> source, int shift, Span<ulong> destination)
    {
        if (shift == 0)
        {
            source.CopyTo(destination);
            return;
        }

        ulong carry = 0;
        for (int i = source.Length - 1; i >= 0; i--)
        {
            ulong current = source[i];
            destination[i] = (current >> shift) | carry;
            carry = current << (64 - shift);
        }
    }

    private static bool SubtractProduct(Span<ulong> value, ReadOnlySpan<ulong> divisor, ulong multiplier)
    {
        ulong borrow = 0;
        for (int i = 0; i < divisor.Length; i++)
        {
            UInt128 product = (UInt128)divisor[i] * multiplier + borrow;
            ulong low = (ulong)product;
            ulong previous = value[i];
            value[i] = previous - low;
            borrow = (ulong)(product >> 64) + (previous < low ? 1UL : 0UL);
        }

        ulong top = value[divisor.Length];
        value[divisor.Length] = top - borrow;
        return top < borrow;
    }

    private static void AddBack(Span<ulong> value, ReadOnlySpan<ulong> divisor)
    {
        ulong carry = 0;
        for (int i = 0; i < divisor.Length; i++)
        {
            UInt128 sum = (UInt128)value[i] + divisor[i] + carry;
            value[i] = (ulong)sum;
            carry = (ulong)(sum >> 64);
        }
        value[divisor.Length] += carry;
    }
}
