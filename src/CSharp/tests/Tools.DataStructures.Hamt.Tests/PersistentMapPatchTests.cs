using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Verifies presence-safe map patches, strict application, inversion, and composition.</summary>
public sealed class PersistentMapPatchTests
{
    /// <summary>Verifies a computed patch transforms its source exactly and preserves both snapshots.</summary>
    [Fact]
    public void BetweenAndApply_TransformSourceExactly()
    {
        var keys = StringComparer.OrdinalIgnoreCase;
        var source = PersistentHashMap<string, string?>.Create(keys)
            .Add("same", "value")
            .Add("change", "before")
            .Add("remove", null);
        var target = source
            .SetItem("CHANGE", "after")
            .Remove("REMOVE")
            .Add("added", null);

        var patch = PersistentMapPatch<string, string?>.Between(source, target);
        var result = patch.Apply(source);

        Assert.Equal(3, patch.Count);
        Assert.True(result.MapEquals(target));
        Assert.Equal("before", source["change"]);
        Assert.True(source.ContainsKey("remove"));
        Assert.False(source.ContainsKey("added"));
        patch.ValidateInvariants();
    }

    /// <summary>Verifies present null and absence remain distinguishable in entries.</summary>
    [Fact]
    public void Entry_PreservesPresentNull()
    {
        var patch = PersistentMapPatch<string, string?>.CreateRange(
            [new("key", MapPatchValue<string?>.Absent, MapPatchValue<string?>.Present(null))]);

        Assert.True(patch.TryGetEntry("key", out var entry));
        Assert.False(entry.Before.IsPresent);
        Assert.True(entry.After.IsPresent);
        Assert.Null(entry.After.Value);
        Assert.Throws<InvalidOperationException>(() => entry.Before.Value);
        Assert.True(patch.Apply(PersistentHashMap<string, string?>.Empty).ContainsKey("key"));
    }

    /// <summary>Verifies a conflict publishes no partial successor and reports the conflicting key.</summary>
    [Fact]
    public void TryApply_ConflictReturnsOriginalSource()
    {
        var source = PersistentHashMap<string, int>.Empty.Add("a", 1).Add("b", 2);
        var patch = PersistentMapPatch<string, int>.CreateRange(
            [
                new("a", MapPatchValue<int>.Present(1), MapPatchValue<int>.Present(10)),
                new("b", MapPatchValue<int>.Present(99), MapPatchValue<int>.Absent),
            ]);

        Assert.False(patch.TryApply(source, out var result, out var conflictingKey));
        Assert.Same(source, result);
        Assert.Equal("b", conflictingKey);
        Assert.Equal(1, source["a"]);
        Assert.Throws<InvalidOperationException>(() => patch.Apply(source));
    }

    /// <summary>Verifies inversion restores the original source.</summary>
    [Fact]
    public void Invert_RestoresSource()
    {
        var source = PersistentHashMap<int, string>.Empty.Add(1, "one").Add(2, "two");
        var target = source.Remove(1).SetItem(2, "TWO").Add(3, "three");
        var patch = PersistentMapPatch<int, string>.Between(source, target);

        Assert.True(patch.Invert().Apply(target).MapEquals(source));
        Assert.Same(PersistentMapPatch<int, string>.Empty,
            PersistentMapPatch<int, string>.Empty.Invert());
    }

    /// <summary>Verifies composition has the same effect as sequential application and drops round trips.</summary>
    [Fact]
    public void Compose_MatchesSequentialApplication()
    {
        var a = PersistentHashMap<int, string>.Empty.Add(1, "one").Add(2, "two");
        var b = a.SetItem(1, "ONE").Remove(2).Add(3, "three");
        var c = b.SetItem(1, "one").SetItem(3, "THREE").Add(4, "four");
        var first = PersistentMapPatch<int, string>.Between(a, b);
        var second = PersistentMapPatch<int, string>.Between(b, c);

        var composed = first.Compose(second);

        Assert.True(composed.Apply(a).MapEquals(c));
        Assert.False(composed.ContainsKey(1));
        Assert.True(first.Apply(a).MapEquals(b));
        composed.ValidateInvariants();
    }

    /// <summary>Verifies composition rejects incompatible policies and intermediate expectations.</summary>
    [Fact]
    public void Compose_RejectsIncompatibleInputs()
    {
        var firstKeys = new DelegatingStringComparer();
        var secondKeys = new DelegatingStringComparer();
        var first = PersistentMapPatch<string, int>.Create(firstKeys).Add(
            new("x", MapPatchValue<int>.Absent, MapPatchValue<int>.Present(1)));
        var otherPolicy = PersistentMapPatch<string, int>.Create(secondKeys).Add(
            new("x", MapPatchValue<int>.Present(1), MapPatchValue<int>.Present(2)));
        var badMiddle = PersistentMapPatch<string, int>.Create(firstKeys).Add(
            new("x", MapPatchValue<int>.Present(9), MapPatchValue<int>.Present(2)));

        Assert.Throws<ArgumentException>(() => first.Compose(otherPolicy));
        Assert.Throws<ArgumentException>(() => first.Compose(badMiddle));
    }

    /// <summary>Verifies configured value equality removes semantic no-ops.</summary>
    [Fact]
    public void ValueComparer_DropsNoOpChanges()
    {
        var values = StringComparer.OrdinalIgnoreCase;
        var source = PersistentHashMap<int, string>.Empty.Add(1, "alpha");
        var target = source.SetItem(1, "ALPHA");
        var patch = PersistentMapPatch<int, string>.Between(source, target, values);

        Assert.True(patch.IsEmpty);
        Assert.Same(values, patch.ValueComparer);
        Assert.Same(source, patch.Apply(source));
        Assert.Same(patch, patch.Add(
            new(2, MapPatchValue<string>.Present("x"), MapPatchValue<string>.Present("X"))));
    }

    /// <summary>Verifies equivalent changed keys are rejected and removal preserves retained policies.</summary>
    [Fact]
    public void AddAndRemove_RespectKeyEquivalence()
    {
        var keys = StringComparer.OrdinalIgnoreCase;
        var patch = PersistentMapPatch<string, int>.Create(keys).Add(
            new("Key", MapPatchValue<int>.Absent, MapPatchValue<int>.Present(1)));

        Assert.Throws<ArgumentException>(() => patch.Add(
            new("KEY", MapPatchValue<int>.Absent, MapPatchValue<int>.Present(2))));
        Assert.False(patch.TryAdd(
            new("key", MapPatchValue<int>.Absent, MapPatchValue<int>.Present(3)), out var unchanged));
        Assert.Same(patch, unchanged);
        var empty = patch.Remove("KEY");
        Assert.True(empty.IsEmpty);
        Assert.Same(keys, empty.KeyComparer);
        Assert.Same(empty, empty.Clear());
    }

    private sealed class DelegatingStringComparer : IEqualityComparer<string>
    {
        public bool Equals(string? x, string? y) => StringComparer.OrdinalIgnoreCase.Equals(x, y);

        public int GetHashCode(string obj) => StringComparer.OrdinalIgnoreCase.GetHashCode(obj);
    }
}
