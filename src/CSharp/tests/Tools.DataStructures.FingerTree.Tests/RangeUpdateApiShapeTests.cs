using System.Reflection;
using System.Runtime.CompilerServices;
using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>Locks the benchmark-independent range-update algebra and sequence API.</summary>
public sealed class RangeUpdateApiShapeTests
{
    /// <summary>The algebra extends ordered measurement with exactly the documented static tag action.</summary>
    [Fact]
    public void Algebra_IsPublicInterfaceWithTheExactStaticSurface()
    {
        var algebra = typeof(IRangeUpdateAlgebra<,,>);
        var parameters = algebra.GetGenericArguments();

        Assert.True(algebra.IsPublic);
        Assert.True(algebra.IsInterface);
        Assert.Equal(new[] { "TElement", "TMeasure", "TTag" }, parameters.Select(static item => item.Name));
        Assert.Contains(
            typeof(IMeasure<,>).MakeGenericType(parameters[0], parameters[1]),
            algebra.GetInterfaces());

        var properties = algebra
            .GetProperties(BindingFlags.Public | BindingFlags.Static | BindingFlags.DeclaredOnly);
        var identityTag = Assert.Single(properties);
        Assert.Equal("IdentityTag", identityTag.Name);
        Assert.Equal(parameters[2], identityTag.PropertyType);
        Assert.Empty(identityTag.GetIndexParameters());
        Assert.Null(identityTag.SetMethod);
        Assert.NotNull(identityTag.GetMethod);
        Assert.True(identityTag.GetMethod!.IsStatic);
        Assert.True(identityTag.GetMethod.IsAbstract);

        var methods = algebra
            .GetMethods(BindingFlags.Public | BindingFlags.Static | BindingFlags.DeclaredOnly)
            .Where(static method => !method.IsSpecialName)
            .ToArray();
        Assert.Equal(
            new[] { "ApplyElement", "ApplyMeasure", "Compose", "IsIdentity" },
            methods.Select(static method => method.Name).Order(StringComparer.Ordinal));

        AssertAbstractStaticMethod(algebra, "IsIdentity", typeof(bool), parameters[2]);
        AssertAbstractStaticMethod(algebra, "Compose", parameters[2], parameters[2], parameters[2]);
        AssertAbstractStaticMethod(algebra, "ApplyElement", parameters[0], parameters[2], parameters[0]);
        AssertAbstractStaticMethod(
            algebra,
            "ApplyMeasure",
            parameters[1],
            parameters[2],
            parameters[1],
            typeof(int));
    }

    /// <summary>The sequence is a sealed read-only list constrained by the matching algebra.</summary>
    [Fact]
    public void Sequence_IsSealedReadOnlyListWithTheExactConstraint()
    {
        var sequence = typeof(RangeUpdateSequence<,,,>);
        var parameters = sequence.GetGenericArguments();

        Assert.True(sequence.IsPublic);
        Assert.True(sequence.IsClass);
        Assert.True(sequence.IsSealed);
        Assert.Equal(
            new[] { "TElement", "TMeasure", "TTag", "TOps" },
            parameters.Select(static item => item.Name));
        Assert.Contains(
            typeof(IReadOnlyList<>).MakeGenericType(parameters[0]),
            sequence.GetInterfaces());
        Assert.Equal(
            new[]
            {
                typeof(IRangeUpdateAlgebra<,,>)
                    .MakeGenericType(parameters[0], parameters[1], parameters[2]),
            },
            parameters[3].GetGenericParameterConstraints());
        Assert.Empty(sequence.GetConstructors(BindingFlags.Public | BindingFlags.Instance));
    }

    /// <summary>The factories, operations, and properties match the generic contract exactly.</summary>
    [Fact]
    public void Sequence_ExposesTheExactFactoriesAndOperations()
    {
        var sequence = typeof(RangeUpdateSequence<,,,>);
        var parameters = sequence.GetGenericArguments();
        var element = parameters[0];
        var measure = parameters[1];
        var tag = parameters[2];
        var enumeratorDefinition = sequence.GetNestedType("Enumerator", BindingFlags.Public)!;
        var enumerator = enumeratorDefinition.MakeGenericType(parameters);
        var cursor = typeof(RangeUpdateSequenceCursor<,,,>).MakeGenericType(parameters);

        var properties = sequence
            .GetProperties(BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly)
            .OrderBy(static property => property.Name, StringComparer.Ordinal)
            .ToArray();
        Assert.Equal(new[] { "Count", "Empty", "IsEmpty", "Item", "Measure" }, properties.Select(static item => item.Name));
        AssertProperty(sequence, "Empty", sequence, isStatic: true);
        AssertProperty(sequence, "Count", typeof(int), isStatic: false);
        AssertProperty(sequence, "IsEmpty", typeof(bool), isStatic: false);
        AssertProperty(sequence, "Measure", measure, isStatic: false);
        AssertProperty(sequence, "Item", element, isStatic: false, typeof(int));

        var methods = sequence
            .GetMethods(BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly)
            .Where(static method => !method.IsSpecialName)
            .ToArray();
        Assert.Equal(
            new[]
            {
                "Append",
                "ApplyRange",
                "Concat",
                "Create",
                "CreateRange",
                "GetCursor",
                "GetEnumerator",
                "GetRange",
                "Insert",
                "MeasureRange",
                "Prepend",
                "RemoveAt",
                "SetItem",
                "SplitAt",
            },
            methods.Select(static method => method.Name).Order(StringComparer.Ordinal));
        Assert.All(methods, static method => Assert.False(method.IsGenericMethod));
        Assert.DoesNotContain(methods, static method => method.Name is "AddRange" or "AssignRange");

        AssertMethod(sequence, "Create", isStatic: true, sequence, typeof(ReadOnlySpan<>).MakeGenericType(element));
        AssertMethod(
            sequence,
            "CreateRange",
            isStatic: true,
            sequence,
            typeof(IEnumerable<>).MakeGenericType(element));
        AssertMethod(sequence, "Prepend", isStatic: false, sequence, element);
        AssertMethod(sequence, "Append", isStatic: false, sequence, element);
        AssertMethod(sequence, "Insert", isStatic: false, sequence, typeof(int), element);
        AssertMethod(sequence, "SetItem", isStatic: false, sequence, typeof(int), element);
        AssertMethod(sequence, "RemoveAt", isStatic: false, sequence, typeof(int));
        AssertMethod(sequence, "Concat", isStatic: false, sequence, sequence);
        AssertMethod(
            sequence,
            "SplitAt",
            isStatic: false,
            typeof(ValueTuple<,>).MakeGenericType(sequence, sequence),
            typeof(int));
        AssertMethod(sequence, "GetRange", isStatic: false, sequence, typeof(int), typeof(int));
        AssertMethod(sequence, "ApplyRange", isStatic: false, sequence, typeof(int), typeof(int), tag);
        AssertMethod(sequence, "MeasureRange", isStatic: false, measure, typeof(int), typeof(int));
        AssertMethod(sequence, "GetCursor", isStatic: false, cursor, typeof(int));
        AssertMethod(sequence, "GetEnumerator", isStatic: false, enumerator);
    }

    /// <summary>The pattern enumerator is the documented concrete, mutable public struct.</summary>
    [Fact]
    public void Enumerator_IsPublicMutableStructWithTheExactSurface()
    {
        var sequence = typeof(RangeUpdateSequence<,,,>);
        var enumerator = sequence.GetNestedType("Enumerator", BindingFlags.Public);

        Assert.NotNull(enumerator);
        Assert.True(enumerator!.IsNestedPublic);
        Assert.True(enumerator.IsValueType);
        Assert.False(enumerator.IsEnum);
        var element = enumerator.GetGenericArguments()[0];
        Assert.DoesNotContain(
            enumerator.CustomAttributes,
            attribute => attribute.AttributeType == typeof(IsReadOnlyAttribute));
        Assert.Contains(
            typeof(IEnumerator<>).MakeGenericType(element),
            enumerator.GetInterfaces());
        Assert.Empty(enumerator.GetConstructors(BindingFlags.Public | BindingFlags.Instance));

        var properties = enumerator
            .GetProperties(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly);
        var current = Assert.Single(properties);
        Assert.Equal("Current", current.Name);
        Assert.Equal(element, current.PropertyType);
        Assert.Empty(current.GetIndexParameters());
        Assert.NotNull(current.GetMethod);
        Assert.Null(current.SetMethod);

        var methods = enumerator
            .GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
            .Where(static method => !method.IsSpecialName)
            .ToArray();
        Assert.Equal(
            new[] { "Dispose", "MoveNext" },
            methods.Select(static method => method.Name).Order(StringComparer.Ordinal));
        AssertMethod(enumerator, "MoveNext", isStatic: false, typeof(bool));
        AssertMethod(enumerator, "Dispose", isStatic: false, typeof(void));
    }

    private static void AssertAbstractStaticMethod(
        Type declaringType,
        string name,
        Type returnType,
        params Type[] parameterTypes)
    {
        var method = AssertMethod(declaringType, name, isStatic: true, returnType, parameterTypes);
        Assert.True(method.IsAbstract);
    }

    private static MethodInfo AssertMethod(
        Type declaringType,
        string name,
        bool isStatic,
        Type returnType,
        params Type[] parameterTypes)
    {
        var method = Assert.Single(
            declaringType.GetMethods(
                BindingFlags.Public
                | BindingFlags.Static
                | BindingFlags.Instance
                | BindingFlags.DeclaredOnly),
            candidate =>
                candidate.Name == name
                && candidate.IsStatic == isStatic
                && candidate.GetParameters().Select(static parameter => parameter.ParameterType)
                    .SequenceEqual(parameterTypes));
        Assert.Equal(returnType, method.ReturnType);
        Assert.False(method.IsGenericMethod);
        return method;
    }

    private static void AssertProperty(
        Type declaringType,
        string name,
        Type propertyType,
        bool isStatic,
        params Type[] indexParameterTypes)
    {
        var property = Assert.Single(
            declaringType.GetProperties(
                BindingFlags.Public
                | BindingFlags.Static
                | BindingFlags.Instance
                | BindingFlags.DeclaredOnly),
            candidate => candidate.Name == name);
        Assert.Equal(propertyType, property.PropertyType);
        Assert.Equal(
            indexParameterTypes,
            property.GetIndexParameters().Select(static parameter => parameter.ParameterType));
        Assert.NotNull(property.GetMethod);
        Assert.Equal(isStatic, property.GetMethod!.IsStatic);
        Assert.Null(property.SetMethod);
    }
}
