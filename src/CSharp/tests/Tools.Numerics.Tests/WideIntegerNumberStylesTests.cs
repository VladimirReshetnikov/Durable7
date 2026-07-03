using System.Globalization;
using System.Reflection;
using Xunit;

namespace Tools.Numerics.Tests;

/// <summary>
/// Verifies that fixed-width integer parsing treats <see cref="NumberStyles"/> as supported flag
/// combinations rather than a handful of exact enum values.
/// </summary>
public sealed class WideIntegerNumberStylesTests
{
    /// <summary>
    /// Gets the fixed-width signed and unsigned integer types covered by the shared style tests.
    /// </summary>
    /// <returns>The test data containing every wide integer type.</returns>
    public static TheoryData<Type> WideIntegerTypes()
    {
        return new TheoryData<Type>
        {
            typeof(UInt256),
            typeof(UInt512),
            typeof(UInt1024),
            typeof(Int256),
            typeof(Int512),
            typeof(Int1024),
        };
    }

    /// <summary>
    /// Verifies decimal and hexadecimal parsers accept the supported style flag combinations.
    /// </summary>
    /// <param name="type">The fixed-width integer type to validate.</param>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void Parse_AcceptsSupportedDecimalAndHexCombinations(Type type)
    {
        Assert.Equal("42", Parse(type, "42", NumberStyles.None).ToString());
        Assert.Equal("42", Parse(type, " +42 ", NumberStyles.Integer).ToString());
        Assert.Equal("42", Parse(type, " 42 ", NumberStyles.AllowLeadingWhite | NumberStyles.AllowTrailingWhite).ToString());
        Assert.Equal("42", Parse(type, " 2A ", NumberStyles.HexNumber).ToString());
    }

    /// <summary>
    /// Verifies unsupported style flags and disallowed syntactic elements are rejected.
    /// </summary>
    /// <param name="type">The fixed-width integer type to validate.</param>
    [Theory]
    [MemberData(nameof(WideIntegerTypes))]
    public void TryParse_RejectsDisallowedStyleElements(Type type)
    {
        Assert.False(TryParse(type, "+42", NumberStyles.None, out _));
        Assert.False(TryParse(type, " +42 ", NumberStyles.AllowLeadingWhite | NumberStyles.AllowTrailingWhite, out _));
        Assert.False(TryParse(type, " 2A ", NumberStyles.AllowHexSpecifier, out _));
        Assert.False(TryParse(type, "+2A", NumberStyles.HexNumber, out _));
        Assert.False(TryParse(type, "1,000", NumberStyles.Integer | NumberStyles.AllowThousands, out _));
    }

    private static object Parse(Type type, string text, NumberStyles style)
    {
        var method = type.GetMethod(
            "Parse",
            BindingFlags.Public | BindingFlags.Static,
            binder: null,
            types: new[] { typeof(string), typeof(NumberStyles), typeof(IFormatProvider) },
            modifiers: null);
        Assert.NotNull(method);
        return method.Invoke(null, new object?[] { text, style, CultureInfo.InvariantCulture })!;
    }

    private static bool TryParse(Type type, string text, NumberStyles style, out object? value)
    {
        var method = type.GetMethod(
            "TryParse",
            BindingFlags.Public | BindingFlags.Static,
            binder: null,
            types: new[] { typeof(string), typeof(NumberStyles), typeof(IFormatProvider), type.MakeByRefType() },
            modifiers: null);
        Assert.NotNull(method);

        object?[] arguments = { text, style, CultureInfo.InvariantCulture, Activator.CreateInstance(type) };
        var parsed = (bool)method.Invoke(null, arguments)!;
        value = arguments[3];
        return parsed;
    }
}
