using CsCheck;
using Xunit;

namespace Tools.DataStructures.Tungsten.Tests;

/// <summary>
/// Property-style generated history tests for <see cref="PersistentList{T}"/> against a
/// <see cref="List{T}"/> model, including retained snapshots for persistence.
/// </summary>
public sealed class PersistentListPropertyTests
{
    private static readonly Gen<(int Op, int Position, int Value)> Operation =
        Gen.Select(Gen.Int[0, 9], Gen.Int[0, 1000], Gen.Int[-1000, 1000],
            (op, position, value) => (op, position, value));

    private static readonly Gen<(int Op, int Position, int Value)[]> History = Operation.Array[0, 120];

    /// <summary>Replays generated edit histories against a list model and retained snapshots.</summary>
    [Fact]
    public void RandomHistory_MatchesListModelAndPreservesSnapshots()
    {
        History.Sample(history =>
        {
            var list = PersistentList<int>.Empty;
            var model = new List<int>();
            var snapshots = new List<(PersistentList<int> List, int[] Model)>();

            foreach (var (op, position, value) in history)
            {
                switch (op)
                {
                    case 0:
                        list = list.Append(value);
                        model.Add(value);
                        break;

                    case 1:
                        list = list.Prepend(value);
                        model.Insert(0, value);
                        break;

                    case 2:
                    {
                        var index = position % (model.Count + 1);
                        list = list.Insert(index, value);
                        model.Insert(index, value);
                        break;
                    }

                    case 3 when model.Count > 0:
                    {
                        var index = position % model.Count;
                        list = list.RemoveAt(index);
                        model.RemoveAt(index);
                        break;
                    }

                    case 4 when model.Count > 0:
                    {
                        var index = position % model.Count;
                        list = list.SetItem(index, value);
                        model[index] = value;
                        break;
                    }

                    case 5:
                    {
                        var count = model.Count == 0 ? 0 : position % (model.Count + 1);
                        list = list.Take(count);
                        model.RemoveRange(count, model.Count - count);
                        break;
                    }

                    case 6:
                    {
                        var count = model.Count == 0 ? 0 : position % (model.Count + 1);
                        list = list.Drop(count);
                        model.RemoveRange(0, count);
                        break;
                    }

                    case 7:
                        list = list.Reverse();
                        model.Reverse();
                        break;

                    case 8:
                    {
                        var suffix = PersistentList.Create(value, value + 1);
                        list = list.Join(suffix);
                        model.Add(value);
                        model.Add(value + 1);
                        break;
                    }

                    default:
                        snapshots.Add((list, model.ToArray()));
                        break;
                }

                AssertMatches(model, list);
            }

            foreach (var (snapshotList, snapshotModel) in snapshots)
                AssertMatches(snapshotModel, snapshotList);
        }, iter: 300);
    }

    private static void AssertMatches(IReadOnlyList<int> model, PersistentList<int> list)
    {
        Assert.Equal(model.Count, list.Count);
        Assert.Equal(model, list.ToArray());
        if (model.Count > 0)
        {
            Assert.Equal(model[0], list.First);
            Assert.Equal(model[^1], list.Last);
            Assert.Equal(model[model.Count / 2], list[model.Count / 2]);
        }
    }
}
