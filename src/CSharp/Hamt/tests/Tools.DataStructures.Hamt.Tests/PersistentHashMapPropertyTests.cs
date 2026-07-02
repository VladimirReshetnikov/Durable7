using CsCheck;
using Xunit;
using IntMap = Tools.DataStructures.Hamt.PersistentHashMap<int, int>;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>
/// Property-style generated history tests for <see cref="PersistentHashMap{TKey, TValue}"/>.
/// </summary>
public sealed class PersistentHashMapPropertyTests
{
    private static readonly Gen<(int Op, int Key, int Value)> Operation =
        Gen.Select(Gen.Int[0, 4], Gen.Int[-40, 40], Gen.Int[-1000, 1000],
            (op, key, value) => (op, key, value));

    private static readonly Gen<(int Op, int Key, int Value)[]> History = Operation.Array[0, 200];

    /// <summary>Replays generated update histories against <see cref="Dictionary{TKey, TValue}"/>.</summary>
    [Fact]
    public void RandomHistory_MatchesDictionaryAndPreservesSnapshots()
    {
        History.Sample(history =>
        {
            var map = IntMap.Empty;
            var model = new Dictionary<int, int>();
            var snapshots = new List<(IntMap Map, Dictionary<int, int> Model)>();

            foreach (var (op, key, value) in history)
            {
                switch (op)
                {
                    case 0:
                        map = map.SetItem(key, value);
                        model[key] = value;
                        break;

                    case 1:
                        map = map.Remove(key);
                        model.Remove(key);
                        break;

                    case 2:
                    {
                        var expectedAdded = !model.ContainsKey(key);
                        var actualAdded = map.TryAdd(key, value, out var added);
                        Assert.Equal(expectedAdded, actualAdded);
                        map = added;
                        if (expectedAdded)
                            model[key] = value;
                        break;
                    }

                    case 3:
                    {
                        var expectedRemoved = model.TryGetValue(key, out var expectedValue);
                        var actualRemoved = map.TryRemove(key, out var removed, out var actualValue);
                        Assert.Equal(expectedRemoved, actualRemoved);
                        if (expectedRemoved)
                        {
                            Assert.Equal(expectedValue, actualValue);
                            model.Remove(key);
                        }

                        map = removed;
                        break;
                    }

                    default:
                        snapshots.Add((map, new Dictionary<int, int>(model)));
                        break;
                }

                AssertMatches(model, map);
                foreach (var snapshot in snapshots.TakeLast(5))
                    AssertMatches(snapshot.Model, snapshot.Map);
            }
        }, iter: 300);
    }

    /// <summary>Replays generated histories under a four-bucket hash to stress collisions and branching.</summary>
    [Fact]
    public void RandomHistory_WithCollidingHashes_MatchesDictionary()
    {
        History.Sample(history =>
        {
            var comparer = new FewBucketsComparer();
            var map = IntMap.Create(comparer);
            var model = new Dictionary<int, int>(comparer);

            foreach (var (op, key, value) in history)
            {
                switch (op)
                {
                    case 0 or 4:
                        map = map.SetItem(key, value);
                        model[key] = value;
                        break;

                    case 1:
                        map = map.Remove(key);
                        model.Remove(key);
                        break;

                    case 2:
                    {
                        var expectedAdded = !model.ContainsKey(key);
                        Assert.Equal(expectedAdded, map.TryAdd(key, value, out var added));
                        map = added;
                        if (expectedAdded)
                            model[key] = value;
                        break;
                    }

                    default:
                    {
                        var expectedRemoved = model.TryGetValue(key, out var expectedValue);
                        var actualRemoved = map.TryRemove(key, out var removed, out var actualValue);
                        Assert.Equal(expectedRemoved, actualRemoved);
                        if (expectedRemoved)
                        {
                            Assert.Equal(expectedValue, actualValue);
                            model.Remove(key);
                        }

                        map = removed;
                        break;
                    }
                }
            }

            AssertMatches(model, map);
        }, iter: 300);
    }

    private sealed class FewBucketsComparer : IEqualityComparer<int>
    {
        public bool Equals(int x, int y) => x == y;

        public int GetHashCode(int obj) => obj & 3;
    }

    private static void AssertMatches(Dictionary<int, int> model, IntMap map)
    {
        Assert.Equal(model.Count, map.Count);
        foreach (var (key, expected) in model)
        {
            Assert.True(map.TryGetValue(key, out var actual));
            Assert.Equal(expected, actual);
        }

        Assert.Equal(
            model.OrderBy(kv => kv.Key).ToArray(),
            map.OrderBy(kv => kv.Key).ToArray());
    }
}
