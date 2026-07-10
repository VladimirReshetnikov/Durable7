using System.Numerics;
using System.Reflection;
using Xunit;

namespace Tools.Numerics.Tests;

/// <summary>Locks floating-point and non-adjacent width conversions to .NET integral conventions.</summary>
public sealed class WideIntegerConversionParityTests
{
    /// <summary>Gets every fixed-width integer type together with its signedness and bit width.</summary>
    public static TheoryData<Type, bool, int> WideIntegerTypes() => new()
    {
        { typeof(UInt256), false, 256 },
        { typeof(UInt512), false, 512 },
        { typeof(UInt1024), false, 1024 },
        { typeof(Int256), true, 256 },
        { typeof(Int512), true, 512 },
        { typeof(Int1024), true, 1024 },
    };

    /// <summary>Verifies double and single conversion behavior, including non-finite and checked cases.</summary>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void BinaryFloatingPointConversions_MatchInt128Policy(Type type, bool isSigned, int bits)
    {
        BigInteger min = isSigned ? -(BigInteger.One << (bits - 1)) : BigInteger.Zero;
        BigInteger max = (BigInteger.One << (bits - (isSigned ? 1 : 0))) - BigInteger.One;

        Assert.Equal(BigInteger.Zero, ToBigInteger(ConvertFrom(type, "op_Explicit", double.NaN)));
        Assert.Equal(max, ToBigInteger(ConvertFrom(type, "op_Explicit", double.PositiveInfinity)));
        Assert.Equal(min, ToBigInteger(ConvertFrom(type, "op_Explicit", double.NegativeInfinity)));
        Assert.Equal(BigInteger.One, ToBigInteger(ConvertFrom(type, "op_Explicit", 1.75)));
        Assert.Equal(isSigned ? -BigInteger.One : BigInteger.Zero,
            ToBigInteger(ConvertFrom(type, "op_Explicit", -1.75)));

        AssertOverflow(() => ConvertFrom(type, "op_CheckedExplicit", double.NaN));
        AssertOverflow(() => ConvertFrom(type, "op_CheckedExplicit", double.PositiveInfinity));
        AssertOverflow(() => ConvertFrom(type, "op_CheckedExplicit", double.NegativeInfinity));
        if (isSigned)
            Assert.Equal(-BigInteger.One, ToBigInteger(ConvertFrom(type, "op_CheckedExplicit", -1.75)));
        else
            AssertOverflow(() => ConvertFrom(type, "op_CheckedExplicit", -1.75));

        Assert.Equal(BigInteger.Zero, ToBigInteger(ConvertFrom(type, "op_Explicit", float.NaN)));
        Assert.Equal(max, ToBigInteger(ConvertFrom(type, "op_Explicit", float.PositiveInfinity)));
        AssertOverflow(() => ConvertFrom(type, "op_CheckedExplicit", float.PositiveInfinity));

        double positiveBoundary = Math.ScaleB(1.0, bits - (isSigned ? 1 : 0));
        if (double.IsFinite(positiveBoundary))
        {
            double finiteAboveMax = positiveBoundary;
            Assert.Equal(max, ToBigInteger(ConvertFrom(type, "op_Explicit", finiteAboveMax)));
            AssertOverflow(() => ConvertFrom(type, "op_CheckedExplicit", finiteAboveMax));

            double finiteInside = Math.BitDecrement(positiveBoundary);
            Assert.Equal(new BigInteger(finiteInside),
                ToBigInteger(ConvertFrom(type, "op_CheckedExplicit", finiteInside)));
        }
        else
        {
            // UInt1024 is wider than every finite double; Double.MaxValue must convert, not clamp to MaxValue.
            Assert.False(isSigned);
            Assert.Equal(new BigInteger(double.MaxValue),
                ToBigInteger(ConvertFrom(type, "op_CheckedExplicit", double.MaxValue)));
        }

        if (isSigned)
        {
            double exactMin = -Math.ScaleB(1.0, bits - 1);
            Assert.Equal(min, ToBigInteger(ConvertFrom(type, "op_CheckedExplicit", exactMin)));
            double finiteBelowMin = Math.BitDecrement(exactMin);
            Assert.True(double.IsFinite(finiteBelowMin));
            Assert.Equal(min, ToBigInteger(ConvertFrom(type, "op_Explicit", finiteBelowMin)));
            AssertOverflow(() => ConvertFrom(type, "op_CheckedExplicit", finiteBelowMin));
        }

        object fortyTwo = ConvertFrom(type, "op_Explicit", 42.0);
        Assert.Equal(42.0, (double)ConvertTo(type, typeof(double), fortyTwo));
        Assert.Equal(42.0f, (float)ConvertTo(type, typeof(float), fortyTwo));
    }

    /// <summary>Verifies decimal conversions truncate fractions and retain decimal overflow behavior.</summary>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void DecimalConversions_MatchInt128Policy(Type type, bool isSigned, int _)
    {
        Assert.Equal(BigInteger.One, ToBigInteger(ConvertFrom(type, "op_Explicit", 1.75m)));
        if (isSigned)
            Assert.Equal(-BigInteger.One, ToBigInteger(ConvertFrom(type, "op_Explicit", -1.75m)));
        else
            AssertOverflow(() => ConvertFrom(type, "op_Explicit", -1.75m));

        object fortyTwo = ConvertFrom(type, "op_Explicit", 42m);
        Assert.Equal(42m, (decimal)ConvertTo(type, typeof(decimal), fortyTwo));
        AssertOverflow(() => ConvertTo(type, typeof(decimal), GetStaticField(type, "MaxValue")));
    }

    /// <summary>Verifies the reported 512↔128 and 1024↔256/128 conversion gaps are closed.</summary>
    [Fact]
    public void NonAdjacentWidthConversions_PreserveUncheckedBitsAndCheckedRanges()
    {
        UInt512 u512 = UInt128.MaxValue;
        Assert.Equal(UInt128.MaxValue, checked((UInt128)u512));
        Assert.Throws<OverflowException>(() => checked((UInt128)(UInt512.One << 128)));

        Int512 i512 = Int128.MinValue;
        Assert.Equal(Int128.MinValue, checked((Int128)i512));
        Assert.Throws<OverflowException>(() => checked((Int128)(Int512.One << 128)));

        UInt256 u256 = UInt256.MaxValue;
        UInt1024 u1024 = u256;
        Assert.Equal(u256, checked((UInt256)u1024));
        Assert.Equal(UInt128.MaxValue, (UInt128)UInt1024.MaxValue);
        Assert.Throws<OverflowException>(() => checked((UInt256)(UInt1024.One << 256)));

        Int256 i256 = Int256.MinValue;
        Int1024 i1024 = i256;
        Assert.Equal(i256, checked((Int256)i1024));
        Assert.Equal((Int128)i256, (Int128)i1024);
        Assert.Throws<OverflowException>(() => checked((Int256)(Int1024.One << 256)));
    }

    private static object ConvertFrom<T>(Type target, string operatorName, T value)
    {
        MethodInfo? method = target.GetMethod(
            operatorName,
            BindingFlags.Public | BindingFlags.Static,
            binder: null,
            types: new[] { typeof(T) },
            modifiers: null);
        Assert.NotNull(method);
        return method.Invoke(null, new object?[] { value })!;
    }

    private static object ConvertTo(Type source, Type target, object value)
    {
        MethodInfo? method = source.GetMethods(BindingFlags.Public | BindingFlags.Static)
            .SingleOrDefault(candidate =>
                candidate.Name == "op_Explicit" &&
                candidate.ReturnType == target &&
                candidate.GetParameters() is [{ ParameterType: var parameterType }] &&
                parameterType == source);
        Assert.NotNull(method);
        return method.Invoke(null, new[] { value })!;
    }

    private static BigInteger ToBigInteger(object value) =>
        (BigInteger)ConvertTo(value.GetType(), typeof(BigInteger), value);

    private static object GetStaticField(Type type, string name) =>
        type.GetField(name, BindingFlags.Public | BindingFlags.Static)!.GetValue(null)!;

    private static void AssertOverflow(Action action)
    {
        TargetInvocationException exception = Assert.Throws<TargetInvocationException>(action);
        Assert.IsType<OverflowException>(exception.InnerException);
    }
}
