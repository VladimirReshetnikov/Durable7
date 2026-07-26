// Tests for the measured rope cursor api shape.

using System.Reflection;
using System.Runtime.CompilerServices;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Locks the public Axis 2 C2 measured-cursor API before implementation details can leak into it.</summary>
public sealed class MeasuredRopeCursorApiShapeTests
{
    /// <summary>The measured cursor is the readonly-struct counterpart of the shipped positional cursor.</summary>
    [Fact]
    public void Cursor_IsPublicReadonlyStructWithTheExactSurface()
    {
        var cursor = typeof(MeasuredRopeCursor<,,>);

        Assert.True(cursor.IsPublic);
        Assert.True(cursor.IsValueType);
        Assert.Contains(
            cursor.CustomAttributes,
            attribute => attribute.AttributeType == typeof(IsReadOnlyAttribute));
        Assert.Empty(cursor.GetConstructors(BindingFlags.Public | BindingFlags.Instance));

        var properties = cursor
            .GetProperties(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
            .Select(property => property.Name)
            .OrderBy(static name => name, StringComparer.Ordinal)
            .ToArray();
        Assert.Equal(
            new[]
            {
                "Count",
                "IsAtEnd",
                "IsAtStart",
                "MeasureAfter",
                "MeasureBefore",
                "Position",
            },
            properties);

        var methods = cursor
            .GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
            .Where(static method => !method.IsSpecialName)
            .Select(method => method.Name)
            .OrderBy(static name => name, StringComparer.Ordinal)
            .ToArray();
        Assert.Equal(
            new[]
            {
                "DeleteNext",
                "DeletePrevious",
                "Insert",
                "InsertRange",
                "MoveNext",
                "MovePrevious",
                "ReplaceNext",
                "Seek",
                "Snapshot",
                "TryPeekNext",
                "TryPeekPrevious",
                "TrySeekByMeasure",
                "TrySeekByMeasure",
            },
            methods);

        var declaredMethods = cursor
            .GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
            .ToArray();
        var measureSeeks = declaredMethods
            .Where(static method => method.Name == "TrySeekByMeasure")
            .ToArray();
        Assert.Equal(2, measureSeeks.Length);
        var genericSeek = Assert.Single(measureSeeks, static method => method.IsGenericMethodDefinition);
        Assert.Single(measureSeeks, static method => !method.IsGenericMethodDefinition);
        AssertStructPredicateConstraint(genericSeek, cursor.GetGenericArguments()[1]);

        var insertRange = Assert.Single(declaredMethods, static method => method.Name == "InsertRange");
        var rangeParameter = Assert.Single(insertRange.GetParameters()).ParameterType;
        Assert.True(rangeParameter.IsGenericType);
        Assert.Equal(typeof(ReadOnlySpan<>), rangeParameter.GetGenericTypeDefinition());
    }

    /// <summary>The measured rope exposes positional and both closure-shaped and closure-free measure factories.</summary>
    [Fact]
    public void MeasuredRope_ExposesTheExactCursorFactories()
    {
        var rope = typeof(MeasuredRope<,,>);
        var methods = rope.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly);

        var getCursor = Assert.Single(methods, static method => method.Name == "GetCursor");
        Assert.Equal(typeof(int), Assert.Single(getCursor.GetParameters()).ParameterType);

        var measureFactories = methods
            .Where(static method => method.Name == "TryGetCursorByMeasure")
            .ToArray();
        Assert.Equal(2, measureFactories.Length);
        var genericFactory = Assert.Single(measureFactories, static method => method.IsGenericMethodDefinition);
        Assert.Single(measureFactories, static method => !method.IsGenericMethodDefinition);
        Assert.All(measureFactories, static method => Assert.Equal(typeof(bool), method.ReturnType));
        AssertStructPredicateConstraint(genericFactory, rope.GetGenericArguments()[1]);
    }

    /// <summary>Text line/column navigation extends the measured cursor rather than introducing another rope type.</summary>
    [Fact]
    public void RopeText_ExposesMeasuredCursorLineColumnOverload()
    {
        var overload = Assert.Single(
            typeof(RopeText).GetMethods(BindingFlags.Public | BindingFlags.Static),
            static method =>
                method.Name == "LineColumnOf" &&
                method.GetParameters() is [{ ParameterType: var parameterType }] &&
                parameterType == typeof(MeasuredRopeCursor<char, int, NewlineMeasure>));

        Assert.Equal(typeof(ValueTuple<int, int>), overload.ReturnType);
    }

    private static void AssertStructPredicateConstraint(MethodInfo method, Type measureParameter)
    {
        var predicateParameter = Assert.Single(method.GetGenericArguments());
        Assert.True(
            (predicateParameter.GenericParameterAttributes & GenericParameterAttributes.NotNullableValueTypeConstraint) != 0);
        Assert.Contains(
            typeof(IMeasurePredicate<>).MakeGenericType(measureParameter),
            predicateParameter.GetGenericParameterConstraints());
    }
}
