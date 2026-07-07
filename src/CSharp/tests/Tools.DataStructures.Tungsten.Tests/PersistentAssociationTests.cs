using Xunit;

namespace Tools.DataStructures.Tungsten.Tests;

/// <summary>
/// Example-based tests for <see cref="PersistentAssociation{TKey, TValue}"/>. The ordering cases
/// mirror the kernel-verified Tungsten Association semantics recorded in the implementation's
/// header comment (verified against Tungsten Engine 14.3 and re-verified against Tungsten 15.0).
/// </summary>
public sealed class PersistentAssociationTests
{
    private static PersistentAssociation<string, int> Assoc(params (string Key, int Value)[] pairs) =>
        PersistentAssociation.CreateRange(pairs.Select(p => KeyValuePair.Create(p.Key, p.Value)));

    private static void AssertOrder(PersistentAssociation<string, int> assoc, params (string Key, int Value)[] expected)
    {
        Assert.Equal(expected.Select(p => KeyValuePair.Create(p.Key, p.Value)), assoc.ToArray());
        Assert.Equal(expected.Select(p => p.Key), assoc.Keys);
        Assert.Equal(expected.Select(p => p.Value), assoc.Values);
        Assert.Equal(expected.Length, assoc.Count);
        for (var i = 0; i < expected.Length; i++)
        {
            Assert.Equal(KeyValuePair.Create(expected[i].Key, expected[i].Value), assoc.GetAt(i));
            Assert.Equal(i, assoc.IndexOfKey(expected[i].Key));
            Assert.True(assoc.TryGetValue(expected[i].Key, out var value));
            Assert.Equal(expected[i].Value, value);
        }
    }

    /// <summary>Verifies the canonical empty association is shared and empty.</summary>
    [Fact]
    public void Empty_IsSharedAndEmpty()
    {
        Assert.Same(PersistentAssociation<string, int>.Empty, PersistentAssociation<string, int>.Empty);
        Assert.Same(PersistentAssociation<string, int>.Empty, PersistentAssociation<string, int>.Create());
        Assert.True(PersistentAssociation<string, int>.Empty.IsEmpty);
        Assert.Empty(PersistentAssociation<string, int>.Empty);
    }

    /// <summary>Tungsten rule 1: a duplicate key keeps its first position with its last value.</summary>
    [Fact]
    public void Construction_DuplicateKeyKeepsFirstPositionWithLastValue()
    {
        // Tungsten: <|a -> 1, b -> 2, a -> 3|> === <|a -> 3, b -> 2|>.
        AssertOrder(Assoc(("a", 1), ("b", 2), ("a", 3)), ("a", 3), ("b", 2));
    }

    /// <summary>Tungsten rule 3: SetItem updates an existing key in place and appends new keys.</summary>
    [Fact]
    public void SetItem_UpdatesExistingKeyInPlaceAndAppendsNewKeys()
    {
        // Tungsten: assoc["a"] = 5 keeps a's position.
        var assoc = Assoc(("a", 1), ("b", 2));
        AssertOrder(assoc.SetItem("a", 5), ("a", 5), ("b", 2));
        AssertOrder(assoc.SetItem("c", 3), ("a", 1), ("b", 2), ("c", 3));
        AssertOrder(assoc, ("a", 1), ("b", 2));
    }

    /// <summary>Tungsten rule 2: Append moves an existing key to the end.</summary>
    [Fact]
    public void Append_MovesExistingKeyToEnd()
    {
        // Tungsten: Append[<|a -> 1, b -> 2|>, a -> 3] === <|b -> 2, a -> 3|>.
        var assoc = Assoc(("a", 1), ("b", 2));
        AssertOrder(assoc.Append("a", 3), ("b", 2), ("a", 3));
        AssertOrder(assoc.Append("c", 3), ("a", 1), ("b", 2), ("c", 3));
    }

    /// <summary>Tungsten rule 2: Prepend moves an existing key to the front.</summary>
    [Fact]
    public void Prepend_MovesExistingKeyToFront()
    {
        // Tungsten: Prepend[<|a -> 1, b -> 2|>, b -> 3] === <|b -> 3, a -> 1|>.
        var assoc = Assoc(("a", 1), ("b", 2));
        AssertOrder(assoc.Prepend("b", 3), ("b", 3), ("a", 1));
        AssertOrder(assoc.Prepend("c", 3), ("c", 3), ("a", 1), ("b", 2));
    }

    /// <summary>Tungsten rule 6: Join keeps this association's positions with the argument's values.</summary>
    [Fact]
    public void Join_KeepsFirstOperandPositionsWithSecondOperandValues()
    {
        // Tungsten: Join[<|a -> 1, b -> 2|>, <|a -> 3, c -> 4|>] === <|a -> 3, b -> 2, c -> 4|>.
        var joined = Assoc(("a", 1), ("b", 2)).Join(Assoc(("a", 3), ("c", 4)));
        AssertOrder(joined, ("a", 3), ("b", 2), ("c", 4));
    }

    /// <summary>Verifies Insert places a new pair before the requested position.</summary>
    [Fact]
    public void Insert_PlacesPairBeforePosition()
    {
        var assoc = Assoc(("a", 1), ("b", 2));
        AssertOrder(assoc.Insert(0, "c", 3), ("c", 3), ("a", 1), ("b", 2));
        AssertOrder(assoc.Insert(1, "c", 3), ("a", 1), ("c", 3), ("b", 2));
        AssertOrder(assoc.Insert(2, "c", 3), ("a", 1), ("b", 2), ("c", 3));
    }

    /// <summary>Tungsten rule 5: an inserted existing key wins position and value.</summary>
    [Fact]
    public void Insert_ExistingKeyWinsInsertedPositionAndValue()
    {
        // Tungsten: Insert[<|a -> 1, b -> 2, c -> 3|>, a -> 9, 3] === <|b -> 2, a -> 9, c -> 3|>
        // (position 3 is interpreted before the old occurrence of a is removed).
        var assoc = Assoc(("a", 1), ("b", 2), ("c", 3));
        AssertOrder(assoc.Insert(2, "a", 9), ("b", 2), ("a", 9), ("c", 3));
        AssertOrder(assoc.Insert(0, "c", 9), ("c", 9), ("a", 1), ("b", 2));
        AssertOrder(assoc.Insert(3, "a", 9), ("b", 2), ("c", 3), ("a", 9));
        AssertOrder(assoc.Insert(0, "a", 9), ("a", 9), ("b", 2), ("c", 3));
    }

    /// <summary>Verifies removal by key, position, ends, and key set.</summary>
    [Fact]
    public void RemoveOperations_ActByKeyAndPosition()
    {
        var assoc = Assoc(("a", 1), ("b", 2), ("c", 3));
        AssertOrder(assoc.Remove("b"), ("a", 1), ("c", 3));
        AssertOrder(assoc.RemoveAt(1), ("a", 1), ("c", 3));
        AssertOrder(assoc.RemoveFirst(), ("b", 2), ("c", 3));
        AssertOrder(assoc.RemoveLast(), ("a", 1), ("b", 2));
        AssertOrder(assoc.RemoveRange(["c", "a", "missing"]), ("b", 2));
        AssertOrder(assoc, ("a", 1), ("b", 2), ("c", 3));

        Assert.True(assoc.TryRemove("b", out var trimmed, out var removedValue));
        Assert.Equal(2, removedValue);
        AssertOrder(trimmed, ("a", 1), ("c", 3));
        Assert.False(assoc.TryRemove("missing", out var unchanged, out _));
        Assert.Same(assoc, unchanged);
    }

    /// <summary>Verifies removing the final entry collapses to the shared empty instance.</summary>
    [Fact]
    public void RemovingLastEntry_CollapsesToSharedEmpty()
    {
        var assoc = Assoc(("a", 1));
        Assert.Same(PersistentAssociation<string, int>.Empty, assoc.Remove("a"));
        Assert.Same(PersistentAssociation<string, int>.Empty, assoc.RemoveAt(0));
        Assert.Same(PersistentAssociation<string, int>.Empty, assoc.Drop(1));
    }

    /// <summary>Tungsten rule 4: Take/Drop/GetRange slice by position, keeping keyed lookups consistent.</summary>
    [Fact]
    public void TakeDropAndGetRange_SliceByPosition()
    {
        // Tungsten: Take[<|a -> 1, b -> 2, c -> 3|>, 2] === <|a -> 1, b -> 2|>,
        //          Drop[<|a -> 1, b -> 2, c -> 3|>, 1] === <|b -> 2, c -> 3|>.
        var assoc = Assoc(("a", 1), ("b", 2), ("c", 3));
        AssertOrder(assoc.Take(2), ("a", 1), ("b", 2));
        AssertOrder(assoc.Drop(1), ("b", 2), ("c", 3));
        AssertOrder(assoc.GetRange(1, 1), ("b", 2));
        Assert.Same(assoc, assoc.Take(3));
        AssertOrder(assoc.Take(0));

        var sliced = assoc.Drop(1);
        Assert.False(sliced.ContainsKey("a"));
        Assert.Equal(-1, sliced.IndexOfKey("a"));
    }

    /// <summary>Verifies Reverse reverses entry order and keeps trivial cases identical.</summary>
    [Fact]
    public void Reverse_ReversesEntryOrder()
    {
        var assoc = Assoc(("a", 1), ("b", 2), ("c", 3));
        AssertOrder(assoc.Reverse(), ("c", 3), ("b", 2), ("a", 1));
        Assert.Same(PersistentAssociation<string, int>.Empty, PersistentAssociation<string, int>.Empty.Reverse());

        var single = Assoc(("a", 1));
        Assert.Same(single, single.Reverse());
    }

    /// <summary>Tungsten rule 7: KeySort orders by keys, Sort by values, both stably.</summary>
    [Fact]
    public void KeySortAndSort_OrderStablyAndStayOrdinaryAssociations()
    {
        // Tungsten: KeySort[<|b -> 2, a -> 1|>] === <|a -> 1, b -> 2|>; Sort orders by values.
        var assoc = Assoc(("b", 2), ("c", 0), ("a", 1));
        AssertOrder(assoc.KeySort(), ("a", 1), ("b", 2), ("c", 0));
        AssertOrder(assoc.Sort(), ("c", 0), ("a", 1), ("b", 2));

        // Stability: equal sort keys keep association order.
        var ties = Assoc(("b", 7), ("a", 7), ("c", 7));
        AssertOrder(ties.Sort(), ("b", 7), ("a", 7), ("c", 7));

        // A sorted result is an ordinary association: later appends go to the end.
        AssertOrder(assoc.KeySort().SetItem("d", 9), ("a", 1), ("b", 2), ("c", 0), ("d", 9));
    }

    /// <summary>Tungsten rule 8: KeyTake follows the requested key order and skips absent keys.</summary>
    [Fact]
    public void KeyTake_FollowsRequestedOrderAndSkipsAbsents()
    {
        var assoc = Assoc(("a", 1), ("b", 2), ("c", 3));
        AssertOrder(assoc.KeyTake(["c", "missing", "a", "c"]), ("c", 3), ("a", 1));
        AssertOrder(assoc.KeyTake([]));
    }

    /// <summary>Verifies First/Last return the end entries.</summary>
    [Fact]
    public void FirstAndLast_ReturnEndEntries()
    {
        var assoc = Assoc(("a", 1), ("b", 2));
        Assert.Equal(KeyValuePair.Create("a", 1), assoc.First);
        Assert.Equal(KeyValuePair.Create("b", 2), assoc.Last);
    }

    /// <summary>Verifies observably unchanged writes return the same instance.</summary>
    [Fact]
    public void NoOpWrites_ReturnSameInstance()
    {
        var assoc = Assoc(("a", 1), ("b", 2));
        Assert.Same(assoc, assoc.SetItem("a", 1));
        Assert.Same(assoc, assoc.Remove("missing"));
        Assert.Same(assoc, assoc.RemoveRange(["missing"]));
        Assert.Same(assoc, assoc.SetItems([]));
        Assert.Same(assoc, assoc.Join(PersistentAssociation<string, int>.Empty));
        Assert.Same(assoc, assoc.Append("b", 2));
        Assert.Same(assoc, assoc.Prepend("a", 1));
        Assert.Same(assoc, PersistentAssociation<string, int>.Empty.Join(assoc));
        Assert.Same(assoc, assoc.SetItems(assoc));
    }

    /// <summary>Verifies keyed reads: indexer, ContainsKey, and TryGetValue.</summary>
    [Fact]
    public void KeyedReads_UseLookupSemantics()
    {
        var assoc = Assoc(("a", 1));
        Assert.Equal(1, assoc["a"]);
        Assert.True(assoc.ContainsKey("a"));
        Assert.False(assoc.ContainsKey("b"));
        Assert.False(assoc.TryGetValue("b", out _));
        Assert.Throws<KeyNotFoundException>(() => assoc["b"]);
    }

    /// <summary>Verifies a custom comparer governs key equality and is preserved by every derivation.</summary>
    [Fact]
    public void CustomComparer_IsPreservedAndGovernsKeyEquality()
    {
        var assoc = PersistentAssociation<string, int>.Create(StringComparer.OrdinalIgnoreCase)
            .SetItem("Alpha", 1)
            .SetItem("beta", 2);

        Assert.Same(StringComparer.OrdinalIgnoreCase, assoc.Comparer);
        Assert.Equal(1, assoc["ALPHA"]);
        Assert.Equal(0, assoc.IndexOfKey("ALPHA"));

        // SetItem keeps the originally stored key instance; Append re-adds the supplied instance.
        var updated = assoc.SetItem("ALPHA", 9);
        Assert.Equal("Alpha", updated.GetAt(0).Key);
        Assert.True(updated.TryGetKey("aLpHa", out var storedKey));
        Assert.Equal("Alpha", storedKey);
        var appended = assoc.Append("ALPHA", 9);
        Assert.Equal("ALPHA", appended.GetAt(1).Key);

        // KeyTake presents the stored key instances, not the requested ones.
        var taken = assoc.KeyTake(["ALPHA"]);
        Assert.Equal("Alpha", taken.GetAt(0).Key);
        Assert.Equal(1, taken["alpha"]);

        // Derived associations keep the comparer.
        Assert.Same(StringComparer.OrdinalIgnoreCase, assoc.Reverse().Comparer);
        Assert.Same(StringComparer.OrdinalIgnoreCase, assoc.Take(1).Comparer);
        Assert.Same(StringComparer.OrdinalIgnoreCase, assoc.Remove("BETA").Comparer);
        Assert.Same(StringComparer.OrdinalIgnoreCase, taken.Comparer);
    }

    /// <summary>Verifies pattern-based and interface enumeration yield pairs in association order.</summary>
    [Fact]
    public void Enumeration_YieldsPairsInAssociationOrder()
    {
        var assoc = Assoc(("a", 1), ("b", 2));
        var seen = new List<KeyValuePair<string, int>>();
        foreach (var pair in assoc)
            seen.Add(pair);
        Assert.Equal([KeyValuePair.Create("a", 1), KeyValuePair.Create("b", 2)], seen);
        Assert.Equal(seen, ((IEnumerable<KeyValuePair<string, int>>)assoc).ToList());
        Assert.Equal(seen, ((IReadOnlyDictionary<string, int>)assoc).ToList());
    }

    /// <summary>Verifies every retained version stays readable after later updates.</summary>
    [Fact]
    public void PersistentVersions_RemainReadableAfterManyUpdates()
    {
        var versions = new List<PersistentAssociation<int, int>> { PersistentAssociation<int, int>.Empty };
        for (var i = 0; i < 100; i++)
            versions.Add(versions[^1].SetItem(i % 25, i));

        for (var i = 0; i < versions.Count; i++)
        {
            Assert.Equal(Math.Min(i, 25), versions[i].Count);
            for (var key = 0; key < Math.Min(i, 25); key++)
            {
                // Along this history key k was last written at the largest j < i with j % 25 == k.
                var expected = key + (i - 1 - key) / 25 * 25;
                Assert.Equal(expected, versions[i][key]);
            }
        }
    }

    /// <summary>Verifies order and lookups survive stamp-gap exhaustion and full relabels.</summary>
    [Fact]
    public void RepeatedSamePointInserts_SurviveRelabeling()
    {
        // Splitting the same stamp gap repeatedly exhausts it and forces full relabels; order
        // and lookups must be unaffected.
        var assoc = Assoc(("start", -1), ("end", -2));
        for (var i = 0; i < 100; i++)
            assoc = assoc.Insert(1, $"k{i}", i);

        Assert.Equal(102, assoc.Count);
        Assert.Equal("start", assoc.GetAt(0).Key);
        Assert.Equal("end", assoc.GetAt(101).Key);
        // Each insert lands at position 1, so the keys read in reverse insertion order.
        for (var i = 0; i < 100; i++)
        {
            Assert.Equal($"k{99 - i}", assoc.GetAt(1 + i).Key);
            Assert.Equal(i, assoc[$"k{i}"]);
            Assert.Equal(1 + (99 - i), assoc.IndexOfKey($"k{i}"));
        }
    }

    /// <summary>Verifies range, null, and empty-state failures throw the documented exceptions.</summary>
    [Fact]
    public void OutOfRangeArguments_Throw()
    {
        var assoc = Assoc(("a", 1), ("b", 2));
        Assert.Throws<ArgumentOutOfRangeException>(() => assoc.GetAt(2));
        Assert.Throws<ArgumentOutOfRangeException>(() => assoc.GetAt(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => assoc.RemoveAt(2));
        Assert.Throws<ArgumentOutOfRangeException>(() => assoc.Insert(3, "c", 3));
        Assert.Throws<ArgumentOutOfRangeException>(() => assoc.Insert(-1, "c", 3));
        Assert.Throws<ArgumentOutOfRangeException>(() => assoc.Take(3));
        Assert.Throws<ArgumentOutOfRangeException>(() => assoc.Drop(-1));
        Assert.Throws<ArgumentNullException>(() => assoc.SetItems(null!));
        Assert.Throws<ArgumentNullException>(() => assoc.Join(null!));
        Assert.Throws<ArgumentNullException>(() => assoc.RemoveRange(null!));
        Assert.Throws<ArgumentNullException>(() => assoc.KeyTake(null!));
        Assert.Throws<ArgumentNullException>(
            () => PersistentAssociation<string, int>.CreateRange(null!));
        Assert.Throws<InvalidOperationException>(() => PersistentAssociation<string, int>.Empty.First);
        Assert.Throws<InvalidOperationException>(() => PersistentAssociation<string, int>.Empty.RemoveFirst());
        Assert.Throws<InvalidOperationException>(() => PersistentAssociation<string, int>.Empty.RemoveLast());
    }
}
