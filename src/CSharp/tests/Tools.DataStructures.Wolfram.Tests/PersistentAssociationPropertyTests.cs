using CsCheck;
using Xunit;

namespace Tools.DataStructures.Wolfram.Tests;

/// <summary>
/// Property-style generated history tests for <see cref="PersistentAssociation{TKey, TValue}"/>
/// against an ordered pair-list model implementing the kernel-verified Wolfram ordering rules,
/// including retained snapshots for persistence.
/// </summary>
public sealed class PersistentAssociationPropertyTests
{
    private static readonly Gen<(int Op, int Key, int Value, int Position)> Operation =
        Gen.Select(Gen.Int[0, 10], Gen.Int[-20, 20], Gen.Int[-1000, 1000], Gen.Int[0, 1000],
            (op, key, value, position) => (op, key, value, position));

    private static readonly Gen<(int Op, int Key, int Value, int Position)[]> History =
        Operation.Array[0, 120];

    /// <summary>Replays generated histories against the ordered model and retained snapshots.</summary>
    [Fact]
    public void RandomHistory_MatchesOrderedModelAndPreservesSnapshots()
    {
        History.Sample(history =>
        {
            var assoc = PersistentAssociation<int, int>.Empty;
            var model = new List<KeyValuePair<int, int>>();
            var snapshots = new List<(PersistentAssociation<int, int> Assoc, KeyValuePair<int, int>[] Model)>();

            foreach (var (op, key, value, position) in history)
            {
                switch (op)
                {
                    case 0:
                    {
                        // SetItem: update in place, or append a new key.
                        assoc = assoc.SetItem(key, value);
                        var at = model.FindIndex(pair => pair.Key == key);
                        if (at >= 0)
                            model[at] = KeyValuePair.Create(key, value);
                        else
                            model.Add(KeyValuePair.Create(key, value));
                        break;
                    }

                    case 1:
                    {
                        // Append: move an existing key to the end.
                        assoc = assoc.Append(key, value);
                        var at = model.FindIndex(pair => pair.Key == key);
                        if (at >= 0)
                            model.RemoveAt(at);
                        model.Add(KeyValuePair.Create(key, value));
                        break;
                    }

                    case 2:
                    {
                        // Prepend: move an existing key to the front.
                        assoc = assoc.Prepend(key, value);
                        var at = model.FindIndex(pair => pair.Key == key);
                        if (at >= 0)
                            model.RemoveAt(at);
                        model.Insert(0, KeyValuePair.Create(key, value));
                        break;
                    }

                    case 3:
                    {
                        assoc = assoc.Remove(key);
                        var at = model.FindIndex(pair => pair.Key == key);
                        if (at >= 0)
                            model.RemoveAt(at);
                        break;
                    }

                    case 4 when model.Count > 0:
                    {
                        var index = position % model.Count;
                        assoc = assoc.RemoveAt(index);
                        model.RemoveAt(index);
                        break;
                    }

                    case 5:
                    {
                        // Insert: the inserted occurrence wins position and value; the position
                        // is interpreted before the old occurrence is removed.
                        var index = position % (model.Count + 1);
                        assoc = assoc.Insert(index, key, value);
                        var at = model.FindIndex(pair => pair.Key == key);
                        var insertAt = index;
                        if (at >= 0)
                        {
                            model.RemoveAt(at);
                            if (at < insertAt)
                                insertAt--;
                        }
                        model.Insert(insertAt, KeyValuePair.Create(key, value));
                        break;
                    }

                    case 6:
                    {
                        var count = model.Count == 0 ? 0 : position % (model.Count + 1);
                        assoc = assoc.Take(count);
                        model.RemoveRange(count, model.Count - count);
                        break;
                    }

                    case 7:
                    {
                        var count = model.Count == 0 ? 0 : position % (model.Count + 1);
                        assoc = assoc.Drop(count);
                        model.RemoveRange(0, count);
                        break;
                    }

                    case 8:
                        assoc = assoc.Reverse();
                        model.Reverse();
                        break;

                    case 9:
                    {
                        // Join keeps existing positions with the argument's values and appends
                        // new keys in argument order.
                        var argument = PersistentAssociation<int, int>.Empty
                            .SetItem(key, value)
                            .SetItem(key + 1, value + 1);
                        assoc = assoc.Join(argument);
                        foreach (var pair in argument)
                        {
                            var at = model.FindIndex(existing => existing.Key == pair.Key);
                            if (at >= 0)
                                model[at] = pair;
                            else
                                model.Add(pair);
                        }
                        break;
                    }

                    default:
                        snapshots.Add((assoc, model.ToArray()));
                        break;
                }

                AssertMatches(model, assoc);
            }

            foreach (var (snapshotAssoc, snapshotModel) in snapshots)
                AssertMatches(snapshotModel, snapshotAssoc);
        }, iter: 300);
    }

    private static void AssertMatches(IReadOnlyList<KeyValuePair<int, int>> model, PersistentAssociation<int, int> assoc)
    {
        Assert.Equal(model.Count, assoc.Count);
        Assert.Equal(model, assoc.ToArray());
        Assert.Equal(model.Select(pair => pair.Key), assoc.Keys);
        Assert.Equal(model.Select(pair => pair.Value), assoc.Values);

        foreach (var pair in model)
        {
            Assert.True(assoc.TryGetValue(pair.Key, out var value));
            Assert.Equal(pair.Value, value);
        }

        if (model.Count > 0)
        {
            Assert.Equal(model[0], assoc.First);
            Assert.Equal(model[^1], assoc.Last);
            var middle = model.Count / 2;
            Assert.Equal(model[middle], assoc.GetAt(middle));
            Assert.Equal(middle, assoc.IndexOfKey(model[middle].Key));
        }
    }
}
