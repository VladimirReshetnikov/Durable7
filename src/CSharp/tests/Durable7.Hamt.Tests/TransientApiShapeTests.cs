using System.Reflection;
using System.Diagnostics.CodeAnalysis;
using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Reflection guards for the deliberately small public transient surface.</summary>
public sealed class TransientApiShapeTests
{
    /// <summary>Locks the exact map transient declarations selected by Axis 2.</summary>
    [Fact]
    public void MapTransient_HasOnlyTheApprovedPublicSurface()
    {
        var outer = typeof(PersistentHashMap<,>);
        var transient = outer.GetNestedType("Transient", BindingFlags.Public)!;

        Assert.True(transient.IsClass);
        Assert.True(transient.IsSealed);
        Assert.Empty(transient.GetConstructors(BindingFlags.Public | BindingFlags.Instance));
        Assert.Contains(
            transient.GetInterfaces(),
            type => type.IsGenericType && type.GetGenericTypeDefinition() == typeof(IReadOnlyDictionary<,>));
        Assert.DoesNotContain(
            transient.GetInterfaces(),
            type => type.IsGenericType && type.GetGenericTypeDefinition() == typeof(IDictionary<,>));
        Assert.True(transient.GetNestedType("Enumerator", BindingFlags.Public)!.IsValueType);

        Assert.Equal(
            new[] { "Comparer", "Count", "Item", "Keys", "Values" },
            transient.GetProperties(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
                .Select(property => property.Name)
                .OrderBy(name => name));
        Assert.Equal(
            new[]
            {
                "Add", "Clear", "ContainsKey", "GetEnumerator", "Persist", "Remove", "SetItem",
                "TryAdd", "TryGetKey", "TryGetValue", "get_Comparer", "get_Count", "get_Item",
                "get_Keys", "get_Values",
            }.OrderBy(name => name),
            transient.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
                .Select(method => method.Name)
                .OrderBy(name => name));

        var create = outer.GetMethod("CreateTransient", BindingFlags.Public | BindingFlags.Static)!;
        var adopt = outer.GetMethod("ToTransient", BindingFlags.Public | BindingFlags.Instance)!;
        Assert.Equal(transient, create.ReturnType.GetGenericTypeDefinition());
        Assert.Equal(transient, adopt.ReturnType.GetGenericTypeDefinition());
        var comparerParameter = Assert.Single(create.GetParameters());
        Assert.True(comparerParameter.IsOptional);
        Assert.Null(comparerParameter.DefaultValue);
        Assert.Equal(typeof(void), transient.GetMethod("Add")!.ReturnType);
        Assert.Equal(typeof(bool), transient.GetMethod("TryAdd")!.ReturnType);
        Assert.Equal(typeof(void), transient.GetMethod("SetItem")!.ReturnType);
        Assert.Equal(typeof(bool), transient.GetMethod("Remove")!.ReturnType);
        Assert.Equal(typeof(void), transient.GetMethod("Clear")!.ReturnType);
        var maybeNull = transient.GetMethod("TryGetValue")!.GetParameters()[1]
            .GetCustomAttribute<MaybeNullWhenAttribute>();
        Assert.NotNull(maybeNull);
        Assert.False(maybeNull.ReturnValue);

        var forbidden = new[]
        {
            "AddRange", "Freeze", "RemoveRange", "SetItems", "Snapshot", "ToBuilder", "ToImmutable",
            "TryRemove", "Update",
        };
        foreach (var name in forbidden)
            Assert.Null(transient.GetMethod(name, BindingFlags.Public | BindingFlags.Instance));
    }

    /// <summary>Locks the set facade and excludes reusable-builder or algebra mutation extras.</summary>
    [Fact]
    public void SetTransient_HasOnlyTheApprovedPublicSurface()
    {
        var outer = typeof(PersistentHashSet<>);
        var transient = outer.GetNestedType("Transient", BindingFlags.Public)!;

        Assert.True(transient.IsClass);
        Assert.True(transient.IsSealed);
        Assert.Empty(transient.GetConstructors(BindingFlags.Public | BindingFlags.Instance));
        Assert.Contains(
            transient.GetInterfaces(),
            type => type.IsGenericType && type.GetGenericTypeDefinition() == typeof(IReadOnlySet<>));
        Assert.DoesNotContain(
            transient.GetInterfaces(),
            type => type.IsGenericType && type.GetGenericTypeDefinition() == typeof(ISet<>));
        Assert.True(transient.GetNestedType("Enumerator", BindingFlags.Public)!.IsValueType);
        Assert.Equal(
            new[] { "Comparer", "Count" },
            transient.GetProperties(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
                .Select(property => property.Name)
                .OrderBy(name => name));
        Assert.Equal(
            new[]
            {
                "Add", "Clear", "Contains", "GetEnumerator", "IsProperSubsetOf", "IsProperSupersetOf",
                "IsSubsetOf", "IsSupersetOf", "Overlaps", "Persist", "Remove", "SetEquals", "TryGetValue",
                "get_Comparer", "get_Count",
            }.OrderBy(name => name),
            transient.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
                .Select(method => method.Name)
                .OrderBy(name => name));

        var create = outer.GetMethod("CreateTransient", BindingFlags.Public | BindingFlags.Static)!;
        var adopt = outer.GetMethod("ToTransient", BindingFlags.Public | BindingFlags.Instance)!;
        Assert.Equal(transient, create.ReturnType.GetGenericTypeDefinition());
        Assert.Equal(transient, adopt.ReturnType.GetGenericTypeDefinition());
        var comparerParameter = Assert.Single(create.GetParameters());
        Assert.True(comparerParameter.IsOptional);
        Assert.Null(comparerParameter.DefaultValue);
        Assert.Equal(typeof(bool), transient.GetMethod("Add")!.ReturnType);
        Assert.Equal(typeof(bool), transient.GetMethod("Remove")!.ReturnType);
        Assert.Equal(typeof(void), transient.GetMethod("Clear")!.ReturnType);

        var forbidden = new[]
        {
            "ExceptWith", "Freeze", "IntersectWith", "Snapshot", "SymmetricExceptWith", "ToBuilder",
            "ToImmutable", "TryAdd", "UnionWith",
        };
        foreach (var name in forbidden)
            Assert.Null(transient.GetMethod(name, BindingFlags.Public | BindingFlags.Instance));
    }
}
