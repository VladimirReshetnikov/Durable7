using CsCheck;
using Xunit;

namespace Durable7.Tungsten.Tests;

/// <summary>
/// Property coverage for association derivations that reconcile positional and keyed state:
/// strict-middle slicing, requested-key projection, and stable value sorting.
/// </summary>
public sealed class PersistentAssociationDerivedPropertyTests
{
    /// <summary>
    /// Checks arbitrary strict-middle ranges and deliberately exercises both <c>GetRange</c> index
    /// reconciliation branches (rebuild from a small kept side and trim a large kept side).
    /// </summary>
    [Fact]
    public void GetRange_StrictMiddleSlices_MatchOrderedModel()
    {
        Gen.Select(Gen.Int.Array[5, 100], Gen.Int, Gen.Int).Sample(sample =>
        {
            var (rawValues, rawIndex, rawCount) = sample;
            var source = CreateIndexed(rawValues);
            var model = source.ToArray();

            var index = 1 + Mod(rawIndex, source.Count - 2);
            var count = 1 + Mod(rawCount, source.Count - index - 1);
            AssertMatches(model.Skip(index).Take(count), source.GetRange(index, count));

            // One kept entry selects the rebuild branch; removing one entry at each end selects
            // the trim branch for every generated source (all have at least five entries).
            var middle = source.Count / 2;
            AssertMatches(model.Skip(middle).Take(1), source.GetRange(middle, 1));
            AssertMatches(model.Skip(1).Take(source.Count - 2), source.GetRange(1, source.Count - 2));

            AssertMatches(model, source); // all derivations leave the retained source snapshot intact
        }, iter: 300);
    }

    /// <summary>
    /// Checks that <c>KeyTake</c> follows first requested occurrence order, skips absent keys,
    /// suppresses duplicate requests, and preserves every selected keyed lookup.
    /// </summary>
    [Fact]
    public void KeyTake_GeneratedRequests_MatchFirstDistinctPresentKeys()
    {
        Gen.Select(Gen.Int.Array[0, 100], Gen.Int.Array[0, 160]).Sample(sample =>
        {
            var (rawValues, rawRequests) = sample;
            var source = CreateIndexed(rawValues);
            var requests = rawRequests.Select(raw => Mod(raw, source.Count + 10) - 5).ToArray();
            var seen = new HashSet<int>();
            var expected = new List<KeyValuePair<int, int>>();
            foreach (var key in requests)
            {
                if ((uint)key < (uint)rawValues.Length && seen.Add(key))
                    expected.Add(KeyValuePair.Create(key, rawValues[key]));
            }

            AssertMatches(expected, source.KeyTake(requests));
            AssertMatches(source.ToArray(), source);
        }, iter: 300);
    }

    /// <summary>
    /// Checks default and custom value sorts against LINQ's stable ordering, using a deliberately
    /// small value domain so generated histories contain many ties.
    /// </summary>
    [Fact]
    public void Sort_GeneratedValues_IsStableAndMatchesModel()
    {
        Gen.Int.Array[0, 120].Sample(rawValues =>
        {
            var values = rawValues.Select(raw => Mod(raw, 11) - 5).ToArray();
            var source = CreateIndexed(values);
            var model = source.ToArray();

            AssertMatches(model.OrderBy(pair => pair.Value), source.Sort());

            var descending = Comparer<int>.Create((left, right) => right.CompareTo(left));
            AssertMatches(model.OrderBy(pair => pair.Value, descending), source.Sort(descending));
            AssertMatches(model, source);
        }, iter: 300);
    }

    private static PersistentAssociation<int, int> CreateIndexed(IReadOnlyList<int> values)
    {
        var association = PersistentAssociation<int, int>.Empty;
        for (var key = 0; key < values.Count; key++)
            association = association.Append(key, values[key]);
        return association;
    }

    private static void AssertMatches(
        IEnumerable<KeyValuePair<int, int>> expected,
        PersistentAssociation<int, int> actual)
    {
        var model = expected.ToArray();
        Assert.Equal(model, actual.ToArray());
        Assert.Equal(model.Select(pair => pair.Key), actual.Keys);
        Assert.Equal(model.Select(pair => pair.Value), actual.Values);

        for (var index = 0; index < model.Length; index++)
        {
            var pair = model[index];
            Assert.Equal(pair, actual.GetAt(index));
            Assert.Equal(index, actual.IndexOfKey(pair.Key));
            Assert.True(actual.TryGetValue(pair.Key, out var value));
            Assert.Equal(pair.Value, value);
        }
    }

    private static int Mod(int value, int modulus) => (int)((uint)value % (uint)modulus);
}
