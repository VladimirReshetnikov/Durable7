using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Deterministic mutable-list model histories with retained snapshots and arbitrary branching.</summary>
public sealed class RangeUpdateModelTests
{
    /// <summary>Runs mixed histories from several fixed seeds, validating every produced and sampled old version.</summary>
    /// <param name="seed">The deterministic history seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(17)]
    [InlineData(91)]
    public void BranchingHistories_MatchMutableListModel(int seed)
    {
        var random = new Random(seed);
        var versions = new List<Version>
        {
            new(RangeUpdateAssert.Create(), []),
            new(RangeUpdateAssert.Create(1, 2, 3, 4), [1, 2, 3, 4]),
        };

        for (var step = 0; step < 220; step++)
        {
            var source = versions[random.Next(versions.Count)];
            var before = source.Model.ToArray();
            var result = ApplyRandomCommand(source, random, step);

            RangeUpdateAssert.Matches(result.Model, result.Sequence);
            Assert.Equal(before, source.Sequence.ToArray());
            Assert.Equal(source.Model, source.Sequence.ToArray());

            if (result.Model.Count > 0)
            {
                var index = random.Next(result.Model.Count + 1);
                var count = random.Next(result.Model.Count - index + 1);
                Assert.Equal(
                    RangeUpdateAssert.Fold(result.Model.Skip(index).Take(count)),
                    result.Sequence.MeasureRange(index, count));
            }

            versions.Add(result);

            var retained = versions[random.Next(versions.Count)];
            RangeUpdateAssert.Matches(retained.Model, retained.Sequence);
        }

        foreach (var version in versions.Where((_, index) => index % 11 == 0))
            RangeUpdateAssert.Matches(version.Model, version.Sequence);
    }

    /// <summary>Checks each command kind at empty, singleton, left, middle, right, and whole-range boundaries.</summary>
    [Fact]
    public void ShortDeterministicHistory_CoversEveryCommandBoundary()
    {
        int[][] models = [[], [1], [1, 2], [1, 2, 3, 4, 5]];
        foreach (var initial in models)
        {
            var source = RangeUpdateAssert.Create(initial);
            for (var index = 0; index <= initial.Length; index++)
            {
                var insertedModel = initial.ToList();
                insertedModel.Insert(index, 99);
                RangeUpdateAssert.Matches(insertedModel, source.Insert(index, 99));

                var split = source.SplitAt(index);
                RangeUpdateAssert.Matches(initial, split.Left.Concat(split.Right));

                for (var count = 0; count <= initial.Length - index; count++)
                {
                    var addModel = initial.ToList();
                    RangeUpdateAssert.Apply(addModel, index, count, RangeUpdateTag.Add(3));
                    RangeUpdateAssert.Matches(
                        addModel,
                        source.ApplyRange(index, count, RangeUpdateTag.Add(3)));

                    var assignModel = initial.ToList();
                    RangeUpdateAssert.Apply(assignModel, index, count, RangeUpdateTag.Assign(-4));
                    RangeUpdateAssert.Matches(
                        assignModel,
                        source.ApplyRange(index, count, RangeUpdateTag.Assign(-4)));
                }
            }

            for (var index = 0; index < initial.Length; index++)
            {
                var setModel = initial.ToArray();
                setModel[index] = -99;
                RangeUpdateAssert.Matches(setModel, source.SetItem(index, -99));

                var removedModel = initial.ToList();
                removedModel.RemoveAt(index);
                RangeUpdateAssert.Matches(removedModel, source.RemoveAt(index));
            }

            RangeUpdateAssert.Matches(initial.Prepend(-1).ToArray(), source.Prepend(-1));
            RangeUpdateAssert.Matches(initial.Append(6).ToArray(), source.Append(6));
            RangeUpdateAssert.Matches(initial.Concat([7, 8]).ToArray(), source.Concat(RangeUpdateAssert.Create(7, 8)));
            RangeUpdateAssert.Matches(initial, source);
        }
    }

    private static Version ApplyRandomCommand(Version source, Random random, int step)
    {
        var model = source.Model.ToList();
        var sequence = source.Sequence;

        switch (random.Next(12))
        {
            case 0 when model.Count < 96:
            {
                var value = Value(random, step);
                model.Add(value);
                sequence = sequence.Append(value);
                break;
            }
            case 1 when model.Count < 96:
            {
                var value = Value(random, step);
                model.Insert(0, value);
                sequence = sequence.Prepend(value);
                break;
            }
            case 2 when model.Count < 96:
            {
                var index = random.Next(model.Count + 1);
                var value = Value(random, step);
                model.Insert(index, value);
                sequence = sequence.Insert(index, value);
                break;
            }
            case 3 when model.Count > 0:
            {
                var index = random.Next(model.Count);
                var value = Value(random, step);
                model[index] = value;
                sequence = sequence.SetItem(index, value);
                break;
            }
            case 4 when model.Count > 0:
            {
                var index = random.Next(model.Count);
                model.RemoveAt(index);
                sequence = sequence.RemoveAt(index);
                break;
            }
            case 5:
            {
                var (index, count) = RandomRange(random, model.Count);
                var tag = RangeUpdateTag.Add(random.Next(-4, 5));
                RangeUpdateAssert.Apply(model, index, count, tag);
                sequence = sequence.ApplyRange(index, count, tag);
                break;
            }
            case 6:
            {
                var (index, count) = RandomRange(random, model.Count);
                var tag = RangeUpdateTag.Assign(random.Next(-25, 26));
                RangeUpdateAssert.Apply(model, index, count, tag);
                sequence = sequence.ApplyRange(index, count, tag);
                break;
            }
            case 7:
            {
                var index = random.Next(model.Count + 1);
                var split = sequence.SplitAt(index);
                sequence = split.Left.Concat(split.Right);
                break;
            }
            case 8:
            {
                var (index, count) = RandomRange(random, model.Count);
                model = model.GetRange(index, count);
                sequence = sequence.GetRange(index, count);
                break;
            }
            case 9 when model.Count < 92:
            {
                var extra = Enumerable.Range(0, random.Next(5))
                    .Select(offset => Value(random, step + offset))
                    .ToArray();
                model.AddRange(extra);
                sequence = sequence.Concat(RangeUpdateAssert.Create(extra));
                break;
            }
            case 10:
            {
                var (index, count) = RandomRange(random, model.Count);
                Assert.Equal(
                    RangeUpdateAssert.Fold(model.Skip(index).Take(count)),
                    sequence.MeasureRange(index, count));
                break;
            }
            default:
            {
                var tag = RangeUpdateTag.DistinctIdentity(random.Next(1, 1000));
                sequence = sequence.ApplyRange(0, sequence.Count, tag);
                break;
            }
        }

        return new Version(sequence, model);
    }

    private static (int Index, int Count) RandomRange(Random random, int size)
    {
        var index = random.Next(size + 1);
        return (index, random.Next(size - index + 1));
    }

    private static int Value(Random random, int step) => random.Next(-50, 51) + (step % 7);

    private sealed record Version(
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra> Sequence,
        List<int> Model);
}
