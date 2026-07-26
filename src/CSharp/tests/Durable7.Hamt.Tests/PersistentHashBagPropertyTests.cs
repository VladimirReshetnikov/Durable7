// Tests for the persistent hash bag property.

using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Deterministic comparer-aware model histories and algebra checks for persistent hash bags.</summary>
public sealed class PersistentHashBagPropertyTests
{
    /// <summary>Replays collision-heavy modular-equivalence histories and checks retained snapshots.</summary>
    [Fact]
    public void DeterministicHistories_WithComparerEquivalenceMatchLinearModel()
    {
        for (var trial = 0; trial < 30; trial++)
        {
            var random = new Random(2026071401 + trial);
            var comparer = new ModularCollisionComparer(modulus: 17, hashBuckets: 3);
            var bag = PersistentHashBag<int>.Create(comparer);
            var model = new LinearBagModel<int>(comparer);
            var snapshots = new List<(PersistentHashBag<int> Bag, LinearBagModel<int> Model)>();

            for (var step = 0; step < 220; step++)
            {
                var item = random.Next(-100, 101);
                var copies = random.Next(0, 8);
                switch (random.Next(0, 8))
                {
                    case 0:
                        bag = bag.Add(item);
                        model.AddCopies(item, 1);
                        break;

                    case 1:
                        bag = bag.AddCopies(item, copies);
                        model.AddCopies(item, copies);
                        break;

                    case 2:
                        bag = bag.Remove(item);
                        model.RemoveCopies(item, 1);
                        break;

                    case 3:
                        bag = bag.RemoveCopies(item, copies);
                        model.RemoveCopies(item, copies);
                        break;

                    case 4:
                        bag = bag.RemoveAll(item);
                        model.RemoveAll(item);
                        break;

                    case 5:
                        snapshots.Add((bag, model.Clone()));
                        break;

                    case 6 when step % 71 == 0:
                        bag = bag.Clear();
                        model.Clear();
                        break;
                }

                AssertQueryMatches(model, bag, random.Next(-120, 121));
                AssertMatches(model, bag);
                foreach (var snapshot in snapshots.TakeLast(4))
                    AssertMatches(snapshot.Model, snapshot.Bag);
            }
        }
    }

    /// <summary>Replays histories under a null-aware case-folding collision policy.</summary>
    [Fact]
    public void DeterministicHistories_WithNullAndCaseFoldingMatchLinearModel()
    {
        for (var trial = 0; trial < 20; trial++)
        {
            var random = new Random(2026072401 + trial);
            var comparer = new NullCaseCollisionComparer();
            var bag = PersistentHashBag<string?>.Create(comparer);
            var model = new LinearBagModel<string?>(comparer);
            var snapshots = new List<(PersistentHashBag<string?> Bag, LinearBagModel<string?> Model)>();

            for (var step = 0; step < 140; step++)
            {
                var item = NextNullableItem(random);
                var copies = random.Next(0, 6);
                switch (random.Next(0, 7))
                {
                    case 0:
                        bag = bag.Add(item);
                        model.AddCopies(item, 1);
                        break;

                    case 1:
                        bag = bag.AddCopies(item, copies);
                        model.AddCopies(item, copies);
                        break;

                    case 2:
                        bag = bag.Remove(item);
                        model.RemoveCopies(item, 1);
                        break;

                    case 3:
                        bag = bag.RemoveCopies(item, copies);
                        model.RemoveCopies(item, copies);
                        break;

                    case 4:
                        bag = bag.RemoveAll(item);
                        model.RemoveAll(item);
                        break;

                    case 5:
                        snapshots.Add((bag, model.Clone()));
                        break;

                    default:
                        if (step % 53 == 0)
                        {
                            bag = bag.Clear();
                            model.Clear();
                        }
                        break;
                }

                AssertQueryMatches(model, bag, NextNullableItem(random));
                AssertMatches(model, bag);
                foreach (var snapshot in snapshots.TakeLast(3))
                    AssertMatches(snapshot.Model, snapshot.Bag);
            }
        }
    }

    /// <summary>Checks all four algebra operations against collision-heavy linear multiset models.</summary>
    [Fact]
    public void DeterministicRandomizedAlgebra_MatchesLinearModels()
    {
        var random = new Random(2026073401);
        for (var trial = 0; trial < 160; trial++)
        {
            var comparer = new ModularCollisionComparer(modulus: 23, hashBuckets: 4);
            var left = PersistentHashBag<int>.Create(comparer);
            var right = PersistentHashBag<int>.Create(comparer);
            var leftModel = new LinearBagModel<int>(comparer);
            var rightModel = new LinearBagModel<int>(comparer);

            for (var index = 0; index < random.Next(0, 90); index++)
            {
                var item = random.Next(-250, 251);
                var copies = random.Next(1, 7);
                left = left.AddCopies(item, copies);
                leftModel.AddCopies(item, copies);
            }

            for (var index = 0; index < random.Next(0, 90); index++)
            {
                var item = random.Next(-250, 251);
                var copies = random.Next(1, 7);
                right = right.AddCopies(item, copies);
                rightModel.AddCopies(item, copies);
            }

            foreach (var operation in AllOperations)
            {
                var actual = Apply(operation, left, right);
                var expected = leftModel.Combine(rightModel, operation);
                AssertMatches(expected, actual);
                Assert.Same(comparer, actual.Comparer);
            }

            AssertMatches(leftModel, left);
            AssertMatches(rightModel, right);
        }
    }

    private static readonly BagOperation[] AllOperations =
    [
        BagOperation.Union,
        BagOperation.Intersect,
        BagOperation.Except,
        BagOperation.Sum,
    ];

    private static PersistentHashBag<int> Apply(
        BagOperation operation,
        PersistentHashBag<int> left,
        PersistentHashBag<int> right) =>
        operation switch
        {
            BagOperation.Union => left.Union(right),
            BagOperation.Intersect => left.Intersect(right),
            BagOperation.Except => left.Except(right),
            BagOperation.Sum => left.Sum(right),
            _ => throw new ArgumentOutOfRangeException(nameof(operation)),
        };

    private static void AssertQueryMatches<T>(
        LinearBagModel<T> model,
        PersistentHashBag<T> bag,
        T item)
    {
        var expectedFound = model.TryGetRepresentative(item, out var expectedRepresentative);
        var actualFound = bag.TryGetValue(item, out var actualRepresentative);

        Assert.Equal(expectedFound, actualFound);
        Assert.Equal(expectedRepresentative, actualRepresentative);
        Assert.Equal(model.CountOf(item), bag.CountOf(item));
        Assert.Equal(expectedFound, bag.Contains(item));
    }

    private static void AssertMatches<T>(
        LinearBagModel<T> model,
        PersistentHashBag<T> bag)
    {
        Assert.Same(model.Comparer, bag.Comparer);
        Assert.Equal(model.DistinctCount, bag.DistinctCount);
        Assert.Equal(model.TotalCount, bag.TotalCount);
        Assert.Equal(model.DistinctCount == 0, bag.IsEmpty);

        var expectedClasses = model.Classes.ToArray();
        var actualEntries = bag.Entries.ToArray();
        Assert.Equal(expectedClasses.Length, actualEntries.Length);
        foreach (var expected in expectedClasses)
        {
            var actual = Assert.Single(
                actualEntries,
                entry => model.Comparer.Equals(expected.Representative, entry.Key));
            Assert.Equal(expected.Count, actual.Value);
            Assert.Equal(expected.Representative, actual.Key);
            Assert.Equal(expected.Count, bag.CountOf(expected.Representative));
            Assert.True(bag.TryGetValue(expected.Representative, out var actualRepresentative));
            Assert.Equal(expected.Representative, actualRepresentative);
        }

        Assert.Equal(actualEntries.Select(entry => entry.Key), bag.DistinctItems);
        var expectedExpanded = actualEntries
            .SelectMany(entry => Enumerable.Repeat(entry.Key, entry.Value))
            .ToArray();
        Assert.Equal(expectedExpanded, bag.ToArray());

        var diagnostics = bag.ValidateCanonicalityForDiagnostics();
        Assert.Equal(model.DistinctCount, diagnostics.DistinctCount);
        Assert.Equal(model.TotalCount, diagnostics.TotalCount);
    }

    private static string? NextNullableItem(Random random) =>
        random.Next(0, 9) switch
        {
            0 or 1 => null,
            2 => NewString("Alpha"),
            3 => NewString("ALPHA"),
            4 => NewString("Beta"),
            5 => NewString("BETA"),
            6 => NewString("Gamma"),
            7 => NewString("GAMMA"),
            _ => NewString("Missing"),
        };

    private static string NewString(string value) => new(value.ToCharArray());

    private enum BagOperation
    {
        Union,
        Intersect,
        Except,
        Sum,
    }

    private sealed class LinearBagModel<T>(IEqualityComparer<T> comparer)
    {
        private readonly List<Entry> _entries = [];
        private long _totalCount;

        internal IEqualityComparer<T> Comparer { get; } = comparer;

        internal int DistinctCount => _entries.Count;

        internal long TotalCount => _totalCount;

        internal IEnumerable<(T Representative, int Count)> Classes
        {
            get
            {
                foreach (var entry in _entries)
                    yield return (entry.Representative, entry.Count);
            }
        }

        internal void AddCopies(T item, int count)
        {
            if (count == 0)
                return;

            var index = FindIndex(item);
            if (index < 0)
            {
                _entries.Add(new Entry(item, count));
            }
            else
            {
                _entries[index].Count = checked(_entries[index].Count + count);
            }

            _totalCount = checked(_totalCount + count);
        }

        internal void RemoveCopies(T item, int count)
        {
            if (count == 0)
                return;

            var index = FindIndex(item);
            if (index < 0)
                return;

            var removed = Math.Min(count, _entries[index].Count);
            if (removed == _entries[index].Count)
                _entries.RemoveAt(index);
            else
                _entries[index].Count -= removed;
            _totalCount = checked(_totalCount - removed);
        }

        internal void RemoveAll(T item)
        {
            var index = FindIndex(item);
            if (index < 0)
                return;

            _totalCount = checked(_totalCount - _entries[index].Count);
            _entries.RemoveAt(index);
        }

        internal void Clear()
        {
            _entries.Clear();
            _totalCount = 0;
        }

        internal int CountOf(T item)
        {
            var index = FindIndex(item);
            return index < 0 ? 0 : _entries[index].Count;
        }

        internal bool TryGetRepresentative(T item, out T representative)
        {
            var index = FindIndex(item);
            if (index < 0)
            {
                representative = item;
                return false;
            }

            representative = _entries[index].Representative;
            return true;
        }

        internal LinearBagModel<T> Clone()
        {
            var clone = new LinearBagModel<T>(Comparer);
            foreach (var entry in _entries)
                clone._entries.Add(new Entry(entry.Representative, entry.Count));
            clone._totalCount = _totalCount;
            return clone;
        }

        internal LinearBagModel<T> Combine(LinearBagModel<T> other, BagOperation operation)
        {
            var result = Clone();
            switch (operation)
            {
                case BagOperation.Union:
                    foreach (var argument in other._entries)
                    {
                        var index = result.FindIndex(argument.Representative);
                        if (index < 0)
                            result.AddCopies(argument.Representative, argument.Count);
                        else if (argument.Count > result._entries[index].Count)
                            result.SetCountAt(index, argument.Count);
                    }
                    break;

                case BagOperation.Intersect:
                    for (var index = result._entries.Count - 1; index >= 0; index--)
                    {
                        var argumentIndex = other.FindIndex(result._entries[index].Representative);
                        var count = argumentIndex < 0
                            ? 0
                            : Math.Min(result._entries[index].Count, other._entries[argumentIndex].Count);
                        result.SetCountAt(index, count);
                    }
                    break;

                case BagOperation.Except:
                    for (var index = result._entries.Count - 1; index >= 0; index--)
                    {
                        var argumentIndex = other.FindIndex(result._entries[index].Representative);
                        if (argumentIndex < 0)
                            continue;
                        result.SetCountAt(
                            index,
                            Math.Max(0, result._entries[index].Count - other._entries[argumentIndex].Count));
                    }
                    break;

                case BagOperation.Sum:
                    foreach (var argument in other._entries)
                        result.AddCopies(argument.Representative, argument.Count);
                    break;

                default:
                    throw new ArgumentOutOfRangeException(nameof(operation));
            }

            return result;
        }

        private int FindIndex(T item)
        {
            for (var index = 0; index < _entries.Count; index++)
            {
                if (Comparer.Equals(_entries[index].Representative, item))
                    return index;
            }

            return -1;
        }

        private void SetCountAt(int index, int count)
        {
            var previous = _entries[index].Count;
            _totalCount = checked(_totalCount + count - previous);
            if (count == 0)
                _entries.RemoveAt(index);
            else
                _entries[index].Count = count;
        }

        private sealed class Entry(T representative, int count)
        {
            internal T Representative { get; } = representative;

            internal int Count { get; set; } = count;
        }
    }

    private sealed class ModularCollisionComparer(int modulus, int hashBuckets) : IEqualityComparer<int>
    {
        public bool Equals(int left, int right) => ClassOf(left) == ClassOf(right);

        public int GetHashCode(int value) => ClassOf(value) % hashBuckets;

        private int ClassOf(int value)
        {
            var remainder = value % modulus;
            return remainder < 0 ? remainder + modulus : remainder;
        }
    }

    private sealed class NullCaseCollisionComparer : IEqualityComparer<string?>
    {
        public bool Equals(string? left, string? right) =>
            StringComparer.OrdinalIgnoreCase.Equals(left, right);

        public int GetHashCode(string? value) => 0;
    }
}
