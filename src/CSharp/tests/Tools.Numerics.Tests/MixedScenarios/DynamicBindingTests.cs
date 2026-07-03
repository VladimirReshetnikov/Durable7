using System.Numerics;
using Microsoft.CSharp.RuntimeBinder;
using Xunit;

namespace Tools.Numerics.Tests;

/// <summary>
/// Verifies C# dynamic-binder behavior where static compile-time operator resolution does not apply.
/// </summary>
/// <remarks>
/// Dynamic binding is inherently a mixed scenario, so this suite intentionally keeps all quadrants together while
/// preserving a stable counterpart order (UInt256, Int256, UInt512, Int512) and explicit mixed tests.
/// </remarks>
public sealed class DynamicBindingTests
{
    /// <summary>
    /// Verifies representative dynamic arithmetic and bitwise operations for <see cref="UInt256"/>.
    /// </summary>
    [Fact]
    public void DynamicUInt256_OperatorsAndConversionsBehaveAsExpected()
    {
        dynamic left = (UInt256)((BigInteger.One << 255) + 10);
        dynamic right = (UInt256)3;

        var quotient = left / right;
        var remainder = left % right;
        var orValue = left | right;

        Assert.IsType<UInt256>(quotient);
        Assert.Equal((UInt256)(((BigInteger.One << 255) + 10) / 3), (UInt256)quotient);
        Assert.Equal((UInt256)(((BigInteger.One << 255) + 10) % 3), (UInt256)remainder);
        Assert.Equal((UInt256)((BigInteger.One << 255) + 11), (UInt256)orValue);
    }

    /// <summary>
    /// Verifies dynamic arithmetic and shift normalization for <see cref="Int256"/> operands.
    /// </summary>
    [Fact]
    public void DynamicInt256_OperatorsResolveToInt256Overloads()
    {
        dynamic left = (Int256)1234567890123456789;
        dynamic right = (Int256)(-9876543210);
        dynamic shift = 260;

        Assert.Equal(1234567880246913579, (Int256)(left + right));
        Assert.Equal((Int256)BigInteger.Parse("19753086241975308624"), (Int256)(left << shift));
        Assert.IsType<Int256>(left + right);
    }

    /// <summary>
    /// Verifies representative dynamic arithmetic and bitwise operations for <see cref="UInt512"/>.
    /// </summary>
    [Fact]
    public void DynamicUInt512_OperatorsAndConversionsBehaveAsExpected()
    {
        dynamic left = (UInt512)((BigInteger.One << 511) + 10);
        dynamic right = (UInt512)3;

        var quotient = left / right;
        var remainder = left % right;
        var orValue = left | right;

        Assert.IsType<UInt512>(quotient);
        Assert.Equal((UInt512)(((BigInteger.One << 511) + 10) / 3), (UInt512)quotient);
        Assert.Equal((UInt512)(((BigInteger.One << 511) + 10) % 3), (UInt512)remainder);
        Assert.Equal((UInt512)((BigInteger.One << 511) + 11), (UInt512)orValue);
    }

    /// <summary>
    /// Verifies dynamic arithmetic and shift normalization for <see cref="Int512"/> operands.
    /// </summary>
    [Fact]
    public void DynamicInt512_OperatorsResolveToInt512Overloads()
    {
        dynamic left = (Int512)((BigInteger.One << 420) + 12345);
        dynamic right = (Int512)(-9876543210);
        dynamic shift = 516;

        Assert.IsType<Int512>(left + right);
        Assert.Equal((Int512)(((BigInteger.One << 420) + 12345) + (-9876543210)), (Int512)(left + right));
        Assert.Equal((Int512)(((BigInteger.One << 420) + 12345) << 4), (Int512)(left << shift));
    }

    /// <summary>
    /// Verifies binder failures and unary behavior for intentionally mixed runtime operand shapes.
    /// </summary>
    [Fact]
    public void DynamicBinding_RuntimeErrorsAndUnarySemantics_AreStable()
    {
        dynamic unsignedValue = (UInt256)42;
        dynamic shiftCountAsLong = 1L;
        Assert.Throws<RuntimeBinderException>(() => _ = unsignedValue << shiftCountAsLong);

        dynamic signed = (Int256)(-1234);
        Assert.Equal(1234, (BigInteger)(-signed));
    }
}
