// Tests for the persistent hash bag api shape.

using System.Collections;
using System.Diagnostics;
using System.Reflection;
using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Reflection and debugger guards for the deliberately closed persistent hash-bag API.</summary>
public sealed class PersistentHashBagApiShapeTests
{
    /// <summary>Locks the exact public type, property, method, and interface surface.</summary>
    [Fact]
    public void Bag_HasOnlyTheApprovedPublicSurface()
    {
        var type = typeof(PersistentHashBag<string>);

        Assert.True(type.IsClass);
        Assert.True(type.IsSealed);
        Assert.Empty(type.GetConstructors(BindingFlags.Public | BindingFlags.Instance));
        Assert.Equal(
            new[] { typeof(IEnumerable), typeof(IEnumerable<string>) }.OrderBy(item => item.FullName),
            type.GetInterfaces().OrderBy(item => item.FullName));
        Assert.False(typeof(IReadOnlyCollection<string>).IsAssignableFrom(type));
        Assert.False(typeof(ICollection<string>).IsAssignableFrom(type));
        Assert.False(typeof(ICollection).IsAssignableFrom(type));

        Assert.Equal(
            new[]
            {
                "Comparer", "DistinctCount", "DistinctItems", "Empty", "Entries", "IsEmpty", "TotalCount",
            },
            type.GetProperties(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
                .Select(property => property.Name)
                .OrderBy(name => name));
        Assert.Null(type.GetProperty("Count", BindingFlags.Public | BindingFlags.Instance));

        Assert.Equal(
            new[]
            {
                "Add", "AddCopies", "Clear", "Contains", "CountOf", "Create", "CreateRange",
                "Except", "GetEnumerator", "Intersect", "Remove", "RemoveAll", "RemoveCopies", "Sum",
                "ToArray", "TryGetValue", "Union", "get_Comparer", "get_DistinctCount",
                "get_DistinctItems", "get_Empty", "get_Entries", "get_IsEmpty", "get_TotalCount",
            }.OrderBy(name => name),
            type.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
                .Select(method => method.Name)
                .OrderBy(name => name));

        Assert.Equal(
            new[] { "Enumerator" },
            type.GetNestedTypes(BindingFlags.Public).Select(nested => nested.Name));
        Assert.Null(type.GetNestedType("Builder", BindingFlags.Public));
        Assert.Null(type.GetNestedType("Transient", BindingFlags.Public));
        Assert.Null(type.GetMethod("Equals", BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly));
        Assert.Null(type.GetMethod("GetHashCode", BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly));
    }

    /// <summary>Locks factory defaults, algebra operands, count widths, views, and try-pattern shape.</summary>
    [Fact]
    public void Members_HaveTheExactApprovedSignatures()
    {
        var type = typeof(PersistentHashBag<string>);
        var create = type.GetMethod(
            "Create",
            BindingFlags.Public | BindingFlags.Static,
            binder: null,
            [typeof(IEqualityComparer<string>)],
            modifiers: null);
        var createRange = type.GetMethod(
            "CreateRange",
            BindingFlags.Public | BindingFlags.Static,
            binder: null,
            [typeof(IEnumerable<string>), typeof(IEqualityComparer<string>)],
            modifiers: null);

        Assert.NotNull(create);
        Assert.NotNull(createRange);
        Assert.Equal(type, create.ReturnType);
        Assert.Equal(type, createRange.ReturnType);
        var comparerParameter = Assert.Single(create.GetParameters());
        Assert.True(comparerParameter.IsOptional);
        Assert.Null(comparerParameter.DefaultValue);
        Assert.Equal(2, createRange.GetParameters().Length);
        Assert.False(createRange.GetParameters()[0].IsOptional);
        Assert.True(createRange.GetParameters()[1].IsOptional);
        Assert.Null(createRange.GetParameters()[1].DefaultValue);

        Assert.Equal(typeof(int), type.GetProperty("DistinctCount")!.PropertyType);
        Assert.Equal(typeof(long), type.GetProperty("TotalCount")!.PropertyType);
        Assert.Equal(typeof(bool), type.GetProperty("IsEmpty")!.PropertyType);
        Assert.Equal(typeof(IEqualityComparer<string>), type.GetProperty("Comparer")!.PropertyType);
        Assert.Equal(typeof(IEnumerable<string>), type.GetProperty("DistinctItems")!.PropertyType);
        Assert.Equal(
            typeof(IEnumerable<KeyValuePair<string, int>>),
            type.GetProperty("Entries")!.PropertyType);
        Assert.Equal(typeof(string[]), type.GetMethod("ToArray")!.ReturnType);

        var tryGetValue = type.GetMethod("TryGetValue")!;
        Assert.Equal(typeof(bool), tryGetValue.ReturnType);
        Assert.Equal(typeof(string), tryGetValue.GetParameters()[0].ParameterType);
        Assert.True(tryGetValue.GetParameters()[1].IsOut);
        Assert.Equal(typeof(string).MakeByRefType(), tryGetValue.GetParameters()[1].ParameterType);

        foreach (var name in new[] { "Union", "Intersect", "Except", "Sum" })
        {
            var method = type.GetMethod(name)!;
            Assert.Equal(type, method.ReturnType);
            Assert.Equal(type, Assert.Single(method.GetParameters()).ParameterType);
        }

        foreach (var forbidden in new[]
                 {
                     "AddRange", "ExceptWith", "Freeze", "IntersectWith", "SetItems", "Snapshot",
                     "SymmetricExcept", "ToBuilder", "ToImmutable", "UnionWith",
                 })
        {
            Assert.Null(type.GetMethod(forbidden, BindingFlags.Public | BindingFlags.Instance));
        }
    }

    /// <summary>Locks the public enumerator as a closed mutable struct implementing the standard contracts.</summary>
    [Fact]
    public void Enumerator_HasTheApprovedValueTypeSurface()
    {
        var type = typeof(PersistentHashBag<string>.Enumerator);

        Assert.True(type.IsValueType);
        Assert.False(type.IsByRefLike);
        Assert.Empty(type.GetConstructors(BindingFlags.Public | BindingFlags.Instance));
        Assert.Contains(typeof(IEnumerator<string>), type.GetInterfaces());
        Assert.Contains(typeof(IEnumerator), type.GetInterfaces());
        Assert.Contains(typeof(IDisposable), type.GetInterfaces());
        Assert.Equal(
            new[] { "Current" },
            type.GetProperties(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
                .Select(property => property.Name));
        Assert.Equal(
            new[] { "Dispose", "MoveNext", "get_Current" }.OrderBy(name => name),
            type.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
                .Select(method => method.Name)
                .OrderBy(name => name));
        Assert.Equal(typeof(string), type.GetProperty("Current")!.PropertyType);
        Assert.Equal(typeof(bool), type.GetMethod("MoveNext")!.ReturnType);
        Assert.Equal(typeof(void), type.GetMethod("Dispose")!.ReturnType);
        Assert.DoesNotContain(
            type.GetFields(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static),
            field => !field.IsSpecialName);
    }

    /// <summary>Verifies debugger metadata projects distinct entries rather than expanded occurrences.</summary>
    [Fact]
    public void DebuggerProxy_IsDistinctAndBoundedByDistinctCount()
    {
        var type = typeof(PersistentHashBag<string>);
        var display = type.GetCustomAttribute<DebuggerDisplayAttribute>();
        var proxyAttribute = type.GetCustomAttribute<DebuggerTypeProxyAttribute>();

        Assert.NotNull(display);
        Assert.Equal("DistinctCount = {DistinctCount}, TotalCount = {TotalCount}", display.Value);
        Assert.NotNull(proxyAttribute);
        Assert.Contains("PersistentHashBagDebugView", proxyAttribute.ProxyTypeName, StringComparison.Ordinal);

        var bag = PersistentHashBag<string>.Empty
            .AddCopies("many", int.MaxValue)
            .AddCopies("few", 2);
        var items = new PersistentHashBagDebugView<string>(bag).Items;

        Assert.Equal(bag.DistinctCount, items.Length);
        Assert.Equal(
            bag.Entries.OrderBy(entry => entry.Key),
            items.OrderBy(entry => entry.Key));
        Assert.Equal(int.MaxValue + 2L, bag.TotalCount);

        var property = typeof(PersistentHashBagDebugView<string>).GetProperty("Items")!;
        var browsable = property.GetCustomAttribute<DebuggerBrowsableAttribute>();
        Assert.NotNull(browsable);
        Assert.Equal(DebuggerBrowsableState.RootHidden, browsable.State);
    }
}
