using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Model and boundary coverage for the 32-bit and 64-bit Patricia map/set families.</summary>
public sealed class PersistentIntegerPatriciaTests
{
    /// <summary>Verifies signed boundary keys enumerate in ascending signed order.</summary>
    [Fact]
    public void Maps_EnumerateInAscendingSignedOrder()
    {
        int[] intKeys = [0, -1, int.MaxValue, int.MinValue, 1, -2, 42];
        long[] longKeys = [0, -1, long.MaxValue, long.MinValue, 1, -2, 1L << 40, -(1L << 40)];

        var ints = PersistentIntMap<string>.CreateRange(intKeys.Select(key => KeyValuePair.Create(key, $"i{key}")));
        var longs = PersistentLongMap<string>.CreateRange(longKeys.Select(key => KeyValuePair.Create(key, $"l{key}")));

        Assert.Equal(intKeys.Order(), ints.Keys);
        Assert.Equal(longKeys.Order(), longs.Keys);
        foreach (var key in intKeys)
            Assert.Equal($"i{key}", ints[key]);
        foreach (var key in longKeys)
            Assert.Equal($"l{key}", longs[key]);
    }

    /// <summary>Checks randomized 32-bit histories and retained snapshots against a dictionary model.</summary>
    [Fact]
    public void IntMap_RandomizedHistoryMatchesModel()
    {
        var random = new Random(20260711);
        var map = PersistentIntMap<int>.Empty;
        var model = new Dictionary<int, int>();
        var snapshots = new List<(PersistentIntMap<int> Map, Dictionary<int, int> Model)>();

        for (var i = 0; i < 20_000; i++)
        {
            var key = random.Next(-2_000, 2_001);
            if (random.Next(4) == 0)
            {
                map = map.Remove(key);
                model.Remove(key);
            }
            else
            {
                var value = random.Next();
                map = map.SetItem(key, value);
                model[key] = value;
            }

            if (i % 997 == 0)
                snapshots.Add((map, new Dictionary<int, int>(model)));
        }

        AssertMap(model, map);
        foreach (var snapshot in snapshots)
            AssertMap(snapshot.Model, snapshot.Map);
    }

    /// <summary>Checks randomized 64-bit histories against a sorted model.</summary>
    [Fact]
    public void LongMap_RandomizedHistoryMatchesModel()
    {
        var random = new Random(20260712);
        var map = PersistentLongMap<long>.Empty;
        var model = new SortedDictionary<long, long>();
        for (var i = 0; i < 15_000; i++)
        {
            var key = random.NextInt64(long.MinValue, long.MaxValue);
            if (random.Next(5) == 0)
            {
                map = map.Remove(key);
                model.Remove(key);
            }
            else
            {
                var value = random.NextInt64();
                map = map.SetItem(key, value);
                model[key] = value;
            }
        }

        Assert.Equal(model.ToArray(), map.ToArray());
    }

    /// <summary>Verifies no-op identity and map merge-combining contracts.</summary>
    [Fact]
    public void MapAlgebra_PreservesNoOpIdentityAndCombinesValues()
    {
        var left = PersistentIntMap<int>.CreateRange(
            new[] { KeyValuePair.Create(1, 10), KeyValuePair.Create(2, 20), KeyValuePair.Create(4, 40) });
        var right = PersistentIntMap<int>.CreateRange(
            new[] { KeyValuePair.Create(2, 3), KeyValuePair.Create(3, 30), KeyValuePair.Create(4, 5) });

        Assert.Same(left, left.SetItem(2, 20));
        Assert.Same(left, left.Remove(999));
        Assert.Same(left.RootIdentity, left.Union(left).RootIdentity);
        Assert.Same(left.RootIdentity, left.Intersect(left).RootIdentity);

        var union = left.Union(right, static (_, l, r) => l + r);
        Assert.Equal(new[] { (1, 10), (2, 23), (3, 30), (4, 45) }, union.Select(pair => (pair.Key, pair.Value)));
        var intersection = left.Intersect(right, static (key, l, r) => key + l + r);
        Assert.Equal(new[] { (2, 25), (4, 49) }, intersection.Select(pair => (pair.Key, pair.Value)));
        Assert.Equal(new[] { 1 }, left.Except(right).Keys);
    }

    /// <summary>Verifies a self-intersection still invokes a supplied non-idempotent combiner.</summary>
    [Fact]
    public void CombiningIntersection_WithSameMapCombinesValuesAtBothWidths()
    {
        var ints = PersistentIntMap<int>.CreateRange(
            new[] { KeyValuePair.Create(-1, 10), KeyValuePair.Create(2, 20) });
        var longs = PersistentLongMap<long>.CreateRange(
            new[] { KeyValuePair.Create(-1L, 100L), KeyValuePair.Create(2L, 200L) });

        var intCalls = 0;
        var combinedInts = ints.Intersect(ints, (key, left, right) =>
        {
            intCalls++;
            return key + left + right;
        });
        var longCalls = 0;
        var combinedLongs = longs.Intersect(longs, (key, left, right) =>
        {
            longCalls++;
            return key + left + right;
        });

        Assert.Equal(ints.Count, intCalls);
        Assert.Equal(new[] { (-1, 19), (2, 42) }, combinedInts.Select(pair => (pair.Key, pair.Value)));
        Assert.Equal(longs.Count, longCalls);
        Assert.Equal(new[] { (-1L, 199L), (2L, 402L) }, combinedLongs.Select(pair => (pair.Key, pair.Value)));
    }

    /// <summary>Verifies both set widths implement persistent algebra and IReadOnlySet relations.</summary>
    [Fact]
    public void Sets_ProvideOrderedPersistentAlgebra()
    {
        var ints = PersistentIntSet.CreateRange([3, -1, 2, 3, int.MinValue]);
        var other = PersistentIntSet.CreateRange([2, 4]);
        Assert.Equal(new[] { int.MinValue, -1, 2, 3 }, ints);
        Assert.Equal(new[] { int.MinValue, -1, 2, 3, 4 }, ints.Union(other));
        Assert.Equal(new[] { 2 }, ints.Intersect(other));
        Assert.Equal(new[] { int.MinValue, -1, 3 }, ints.Except(other));
        Assert.True(ints.IsSupersetOf([-1, 2]));
        Assert.True(ints.Overlaps([100, 3]));

        var longs = PersistentLongSet.CreateRange([long.MaxValue, 0, long.MinValue, -1, 0]);
        Assert.Equal(new[] { long.MinValue, -1L, 0L, long.MaxValue }, longs);
        Assert.Same(longs.RootIdentity, longs.Add(0).RootIdentity);
        Assert.Same(longs.RootIdentity, longs.Remove(42).RootIdentity);
    }

    /// <summary>Checks prefix-aware structural algebra against dictionary models.</summary>
    [Fact]
    public void StructuralAlgebra_RandomizedMapsMatchModels()
    {
        var random = new Random(20260713);
        for (var trial = 0; trial < 200; trial++)
        {
            var leftModel = Enumerable.Range(0, random.Next(0, 200))
                .Select(_ => KeyValuePair.Create(random.Next(-500, 501), random.Next()))
                .GroupBy(pair => pair.Key).ToDictionary(group => group.Key, group => group.Last().Value);
            var rightModel = Enumerable.Range(0, random.Next(0, 200))
                .Select(_ => KeyValuePair.Create(random.Next(-500, 501), random.Next()))
                .GroupBy(pair => pair.Key).ToDictionary(group => group.Key, group => group.Last().Value);
            var left = PersistentIntMap<int>.CreateRange(leftModel);
            var right = PersistentIntMap<int>.CreateRange(rightModel);

            var unionModel = new Dictionary<int, int>(leftModel);
            foreach (var pair in rightModel)
                unionModel[pair.Key] = pair.Value;
            var intersectionModel = leftModel.Where(pair => rightModel.ContainsKey(pair.Key)).ToDictionary();
            var exceptModel = leftModel.Where(pair => !rightModel.ContainsKey(pair.Key)).ToDictionary();

            AssertMap(unionModel, left.Union(right));
            AssertMap(intersectionModel, left.Intersect(right));
            AssertMap(exceptModel, left.Except(right));
        }
    }

    private static void AssertMap(Dictionary<int, int> expected, PersistentIntMap<int> actual)
    {
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected.OrderBy(pair => pair.Key), actual);
        foreach (var (key, value) in expected)
            Assert.Equal(value, actual[key]);
    }
}
