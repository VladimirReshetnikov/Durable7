// Tests for the persistent ordered set api shape.

using System.Collections;
using System.Diagnostics;
using System.Reflection;
using Xunit;

namespace Durable7.Ordered.Tests;

/// <summary>Reflection guards for the independently owned Ordered public surface.</summary>
public sealed class PersistentOrderedSetApiShapeTests
{
    /// <summary>Locks the type, interface, property, nested-type, and constructor shape.</summary>
    [Fact]
    public void OrderedSet_HasOnlyTheApprovedTypeAndPropertySurface()
    {
        var type = typeof(PersistentOrderedSet<string?>);

        Assert.Equal("Durable7.Ordered", type.Namespace);
        Assert.Equal("Durable7.Ordered", type.Assembly.GetName().Name);
        Assert.True(type.IsClass);
        Assert.True(type.IsSealed);
        Assert.Empty(type.GetConstructors(BindingFlags.Public | BindingFlags.Instance));
        Assert.Equal(
            new[]
            {
                typeof(IEnumerable),
                typeof(IEnumerable<string?>),
                typeof(IReadOnlyCollection<string?>),
                typeof(IReadOnlySet<string?>),
            }.OrderBy(item => item.FullName),
            type.GetInterfaces().OrderBy(item => item.FullName));

        Assert.Equal(
            new[] { "Comparer", "Count", "Empty", "First", "IsEmpty", "Item", "Last" },
            type.GetProperties(
                    BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
                .Select(property => property.Name)
                .OrderBy(name => name));
        Assert.Equal(typeof(string), type.GetProperty("Item")!.PropertyType);
        Assert.Equal(typeof(int), Assert.Single(type.GetProperty("Item")!.GetIndexParameters()).ParameterType);
        Assert.Equal(typeof(IEqualityComparer<string?>), type.GetProperty("Comparer")!.PropertyType);

        Assert.Equal(
            new[] { "Enumerator" },
            type.GetNestedTypes(BindingFlags.Public).Select(nested => nested.Name));
        Assert.Null(type.GetNestedType("Builder", BindingFlags.Public));
        Assert.Null(type.GetNestedType("Transient", BindingFlags.Public));
        Assert.Null(type.GetMethod("Equals", BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly));
        Assert.Null(type.GetMethod("GetHashCode", BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly));
    }

    /// <summary>Locks the complete declared method-name multiset, including approved overload counts.</summary>
    [Fact]
    public void OrderedSet_HasOnlyTheApprovedMethodSurface()
    {
        var actual = typeof(PersistentOrderedSet<string>)
            .GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
            .Select(method => method.Name)
            .OrderBy(name => name)
            .ToArray();
        var expected = new[]
        {
            "Add", "AddFirst", "Clear", "Contains", "Create", "CreateRange", "Drop",
            "Except", "Except", "GetAt", "GetCursor", "GetEnumerator", "GetRange", "IndexOf", "Insert",
            "Intersect", "Intersect", "IsProperSubsetOf", "IsProperSupersetOf", "IsSubsetOf",
            "IsSupersetOf", "MoveTo", "MoveToFirst", "MoveToLast", "Overlaps", "Remove",
            "RemoveAt", "RemoveFirst", "RemoveLast", "Reverse", "SetEquals", "Sort",
            "SymmetricExcept", "SymmetricExcept", "Take", "ToArray", "TryGetValue", "TryRemove",
            "TryGetCursor", "Union", "Union",
            "get_Comparer", "get_Count", "get_Empty", "get_First", "get_IsEmpty", "get_Item", "get_Last",
        }.OrderBy(name => name).ToArray();

        Assert.Equal(expected, actual);

        foreach (var forbidden in new[]
                 {
                     "Append", "CopyTo", "Join", "KeySort", "KeyTake", "LowerBound", "Max", "Min",
                     "Prepend", "RemoveRange", "SetItem", "SetItems", "ToBuilder", "ToTransient",
                 })
        {
            Assert.Null(typeof(PersistentOrderedSet<string>)
                .GetMethod(forbidden, BindingFlags.Public | BindingFlags.Instance));
        }
    }

    /// <summary>Locks factory defaults, movement indexing, try-patterns, algebra, and relation signatures.</summary>
    [Fact]
    public void Members_HaveTheApprovedSignatures()
    {
        var type = typeof(PersistentOrderedSet<string>);
        var create = type.GetMethod("Create", BindingFlags.Public | BindingFlags.Static)!;
        var createRange = type.GetMethod(
            "CreateRange",
            BindingFlags.Public | BindingFlags.Static,
            binder: null,
            [typeof(IEnumerable<string>), typeof(IEqualityComparer<string>)],
            modifiers: null)!;
        Assert.Equal(type, create.ReturnType);
        Assert.True(Assert.Single(create.GetParameters()).IsOptional);
        Assert.Null(Assert.Single(create.GetParameters()).DefaultValue);
        Assert.Equal(type, createRange.ReturnType);
        Assert.False(createRange.GetParameters()[0].IsOptional);
        Assert.True(createRange.GetParameters()[1].IsOptional);
        Assert.Null(createRange.GetParameters()[1].DefaultValue);

        Assert.Equal(
            new[] { typeof(int), typeof(string) },
            type.GetMethod("MoveTo")!.GetParameters().Select(parameter => parameter.ParameterType));
        Assert.Equal(
            new[] { typeof(int), typeof(string) },
            type.GetMethod("Insert")!.GetParameters().Select(parameter => parameter.ParameterType));

        var tryGet = type.GetMethod("TryGetValue")!;
        Assert.Equal(typeof(bool), tryGet.ReturnType);
        Assert.Equal(typeof(string), tryGet.GetParameters()[0].ParameterType);
        Assert.True(tryGet.GetParameters()[1].IsOut);
        Assert.Equal(typeof(string).MakeByRefType(), tryGet.GetParameters()[1].ParameterType);

        var tryRemove = type.GetMethod("TryRemove")!;
        Assert.Equal(typeof(bool), tryRemove.ReturnType);
        Assert.Equal(2, tryRemove.GetParameters().Length);
        Assert.True(tryRemove.GetParameters()[1].IsOut);
        Assert.Equal(type.MakeByRefType(), tryRemove.GetParameters()[1].ParameterType);

        foreach (var name in new[] { "Union", "Intersect", "Except", "SymmetricExcept" })
        {
            var operands = type.GetMethods(BindingFlags.Public | BindingFlags.Instance)
                .Where(method => method.Name == name)
                .Select(method => Assert.Single(method.GetParameters()).ParameterType)
                .OrderBy(parameterType => parameterType.FullName)
                .ToArray();
            Assert.Equal(new[] { typeof(IEnumerable<string>), type }.OrderBy(item => item.FullName), operands);
        }

        foreach (var name in new[]
                 {
                     "IsSubsetOf", "IsProperSubsetOf", "IsSupersetOf", "IsProperSupersetOf", "Overlaps", "SetEquals",
                 })
        {
            var method = type.GetMethod(name)!;
            Assert.Equal(typeof(bool), method.ReturnType);
            Assert.Equal(typeof(IEnumerable<string>), Assert.Single(method.GetParameters()).ParameterType);
        }

        var sortParameter = Assert.Single(type.GetMethod("Sort")!.GetParameters());
        Assert.Equal(typeof(IComparer<string>), sortParameter.ParameterType);
        Assert.True(sortParameter.IsOptional);
        Assert.Null(sortParameter.DefaultValue);
    }

    /// <summary>Locks the public enumerator as a mutable value type with standard interface contracts.</summary>
    [Fact]
    public void Enumerator_HasTheApprovedValueTypeSurface()
    {
        var type = typeof(PersistentOrderedSet<string>.Enumerator);

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
        Assert.Empty(type.GetFields(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static));
    }

    /// <summary>Verifies debugger metadata projects only stored representatives in set order.</summary>
    [Fact]
    public void DebuggerView_IsOrderedAndBoundedByCount()
    {
        var type = typeof(PersistentOrderedSet<Representative>);
        var display = type.GetCustomAttribute<DebuggerDisplayAttribute>();
        var proxy = type.GetCustomAttribute<DebuggerTypeProxyAttribute>();
        Assert.NotNull(display);
        Assert.Equal("Count = {Count}", display.Value);
        Assert.NotNull(proxy);
        Assert.Contains("PersistentOrderedSetDebugView", proxy.ProxyTypeName, StringComparison.Ordinal);

        var comparer = new RepresentativeComparer();
        var first = new Representative(1, "first");
        var second = new Representative(2, "second");
        var set = PersistentOrderedSet<Representative>.Create(comparer).Add(first).Add(second);
        var items = new PersistentOrderedSetDebugView<Representative>(set).Items;
        OrderedSetAssert.AssertReferenceSequence(new[] { first, second }, items);
        Assert.Equal(set.Count, items.Length);

        var property = typeof(PersistentOrderedSetDebugView<Representative>).GetProperty("Items")!;
        var browsable = property.GetCustomAttribute<DebuggerBrowsableAttribute>();
        Assert.NotNull(browsable);
        Assert.Equal(DebuggerBrowsableState.RootHidden, browsable.State);
    }
}
