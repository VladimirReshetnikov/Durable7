// Tests for the persistent ordered set property.

using CsCheck;
using Xunit;

namespace Durable7.Ordered.Tests;

/// <summary>Generated branching command histories against an independent comparer-aware list model.</summary>
public sealed class PersistentOrderedSetPropertyTests
{
    private static readonly Gen<(int Operation, int Class, int Identity, int Position)> Command =
        Gen.Select(
            Gen.Int[0, 17],
            Gen.Int[-20, 20],
            Gen.Int,
            Gen.Int,
            (operation, @class, identity, position) => (operation, @class, identity, position));

    private static readonly Gen<(int Operation, int Class, int Identity, int Position)[]> History =
        Command.Array[0, 70];

    private static readonly IComparer<Representative> RemainderOrder = Comparer<Representative>.Create(
        (left, right) => Mod(left.EquivalenceClass, 5).CompareTo(Mod(right.EquivalenceClass, 5)));

    /// <summary>Replays generated branching histories and validates every retained version and both indexes.</summary>
    [Fact]
    public void GeneratedBranchingHistories_MatchIndependentOrderedModel()
    {
        History.Sample(history =>
        {
            var comparer = new RepresentativeComparer();
            var empty = PersistentOrderedSet<Representative>.Create(comparer);
            var versions = new List<(PersistentOrderedSet<Representative> Actual, OrderedListModel<Representative> Model)>
            {
                (empty, new OrderedListModel<Representative>(comparer)),
            };

            foreach (var (operation, @class, identity, position) in history)
            {
                var branch = versions[Mod(identity, versions.Count)];
                var source = branch.Actual;
                var model = branch.Model.Clone();
                var item = new Representative(@class, $"representative-{@class}-{identity}");
                PersistentOrderedSet<Representative> actual;

                switch (operation)
                {
                    case 0:
                    {
                        var changed = model.Add(item);
                        actual = source.Add(item);
                        AssertIdentity(changed, source, actual);
                        break;
                    }

                    case 1:
                    {
                        var changed = model.AddFirst(item);
                        actual = source.AddFirst(item);
                        AssertIdentity(changed, source, actual);
                        break;
                    }

                    case 2:
                    {
                        var index = Mod(position, model.Count + 1);
                        var changed = model.Insert(index, item);
                        actual = source.Insert(index, item);
                        AssertIdentity(changed, source, actual);
                        break;
                    }

                    case 3 when model.Count > 0:
                    {
                        var old = model[Mod(@class, model.Count)];
                        var probe = new Representative(old.EquivalenceClass, $"move-probe-{identity}");
                        var finalIndex = Mod(position, model.Count);
                        var changed = model.MoveTo(finalIndex, probe);
                        actual = source.MoveTo(finalIndex, probe);
                        AssertIdentity(changed, source, actual);
                        break;
                    }

                    case 4:
                    {
                        var changed = model.Remove(item);
                        actual = source.Remove(item);
                        AssertIdentity(changed, source, actual);
                        break;
                    }

                    case 5 when model.Count > 0:
                    {
                        var index = Mod(position, model.Count);
                        model.RemoveAt(index);
                        actual = source.RemoveAt(index);
                        break;
                    }

                    case 6:
                    {
                        var index = Mod(position, model.Count + 1);
                        var count = Mod(identity, model.Count - index + 1);
                        var full = index == 0 && count == model.Count;
                        model.GetRange(index, count);
                        actual = source.GetRange(index, count);
                        if (full)
                            Assert.Same(source, actual);
                        break;
                    }

                    case 7:
                    {
                        var trivial = model.Count <= 1;
                        model.Reverse();
                        actual = source.Reverse();
                        if (trivial)
                            Assert.Same(source, actual);
                        break;
                    }

                    case 8:
                    {
                        var before = model.Items.ToArray();
                        model.StableSort(RemainderOrder);
                        actual = source.Sort(RemainderOrder);
                        if (ReferenceSequenceEqual(before, model.Items))
                            Assert.Same(source, actual);
                        break;
                    }

                    case 9:
                    {
                        Representative[] argument =
                        [
                            item,
                            new Representative(@class, $"duplicate-{identity}"),
                            new Representative(@class + 1000, $"new-{identity}"),
                        ];
                        var oldCount = model.Count;
                        model.Union(argument);
                        actual = source.Union((IEnumerable<Representative>)argument);
                        if (model.Count == oldCount)
                            Assert.Same(source, actual);
                        break;
                    }

                    case 10:
                    {
                        Representative[] argument = [item, new Representative(@class, $"duplicate-{identity}")];
                        var oldCount = model.Count;
                        model.Except(argument);
                        actual = source.Except((IEnumerable<Representative>)argument);
                        if (model.Count == oldCount)
                            Assert.Same(source, actual);
                        break;
                    }

                    case 11:
                    {
                        Representative[] argument =
                        [item, new Representative(@class + 1, $"next-{identity}")];
                        var before = model.Items.ToArray();
                        model.Intersect(argument);
                        actual = source.Intersect((IEnumerable<Representative>)argument);
                        if (ReferenceSequenceEqual(before, model.Items))
                            Assert.Same(source, actual);
                        break;
                    }

                    case 12:
                    {
                        Representative[] argument =
                        [item, new Representative(@class, $"duplicate-{identity}"), new Representative(@class + 1, $"next-{identity}")];
                        model.SymmetricExcept(argument);
                        actual = source.SymmetricExcept((IEnumerable<Representative>)argument);
                        break;
                    }

                    case 13:
                    {
                        var count = Mod(position, model.Count + 1);
                        var full = count == model.Count;
                        model.GetRange(0, count);
                        actual = source.Take(count);
                        if (full)
                            Assert.Same(source, actual);
                        break;
                    }

                    case 14:
                    {
                        var count = Mod(position, model.Count + 1);
                        var identityDrop = count == 0;
                        model.GetRange(count, model.Count - count);
                        actual = source.Drop(count);
                        if (identityDrop)
                            Assert.Same(source, actual);
                        break;
                    }

                    case 15 when model.Count > 0:
                    {
                        var old = model[Mod(@class, model.Count)];
                        var probe = new Representative(old.EquivalenceClass, $"end-probe-{identity}");
                        var finalIndex = (identity & 1) == 0 ? 0 : model.Count - 1;
                        var changed = model.MoveTo(finalIndex, probe);
                        actual = finalIndex == 0 ? source.MoveToFirst(probe) : source.MoveToLast(probe);
                        AssertIdentity(changed, source, actual);
                        break;
                    }

                    case 16:
                    default:
                    {
                        var wasEmpty = model.Count == 0;
                        model.Clear();
                        actual = source.Clear();
                        if (wasEmpty)
                            Assert.Same(source, actual);
                        break;
                    }
                }

                AssertState(model, actual, comparer);
                versions.Add((actual, model));
            }

            foreach (var (actual, model) in versions)
                AssertState(model, actual, comparer);
        }, iter: 120);
    }

    private static void AssertState(
        OrderedListModel<Representative> model,
        PersistentOrderedSet<Representative> actual,
        RepresentativeComparer comparer)
    {
        Assert.Same(comparer, actual.Comparer);
        OrderedSetAssert.Matches(model.Items, actual);
        for (var index = 0; index < model.Count; index++)
        {
            var expected = model[index];
            var probe = new Representative(expected.EquivalenceClass, $"probe-{index}");
            Assert.True(actual.Contains(probe));
            Assert.Equal(index, actual.IndexOf(probe));
            Assert.True(actual.TryGetValue(probe, out var representative));
            Assert.Same(expected, representative);
        }

        var missing = new Representative(100_000, "missing");
        Assert.False(actual.Contains(missing));
        Assert.Equal(-1, actual.IndexOf(missing));
        Assert.False(actual.TryGetValue(missing, out var echoed));
        Assert.Same(missing, echoed);
    }

    private static void AssertIdentity(
        bool changed,
        PersistentOrderedSet<Representative> source,
        PersistentOrderedSet<Representative> actual)
    {
        if (changed)
            Assert.NotSame(source, actual);
        else
            Assert.Same(source, actual);
    }

    private static bool ReferenceSequenceEqual<T>(IReadOnlyList<T> left, IReadOnlyList<T> right)
    {
        if (left.Count != right.Count)
            return false;
        for (var index = 0; index < left.Count; index++)
        {
            if (!ReferenceEquals(left[index], right[index]))
                return false;
        }
        return true;
    }

    private static int Mod(int value, int modulus) => (int)((uint)value % (uint)modulus);
}
