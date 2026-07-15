using Xunit;

namespace Tools.DataStructures.Ordered.Tests;

internal sealed class Representative(int equivalenceClass, string name)
{
    internal int EquivalenceClass { get; } = equivalenceClass;

    internal string Name { get; } = name;

    public override string ToString() => $"{Name}:{EquivalenceClass}";
}

internal class RepresentativeComparer(int hashBuckets = 1) : IEqualityComparer<Representative?>
{
    private readonly int _hashBuckets = hashBuckets > 0
        ? hashBuckets
        : throw new ArgumentOutOfRangeException(nameof(hashBuckets));

    internal int HashCalls { get; private set; }

    internal int EqualityCalls { get; private set; }

    public virtual bool Equals(Representative? left, Representative? right)
    {
        EqualityCalls++;
        return left?.EquivalenceClass == right?.EquivalenceClass;
    }

    public virtual int GetHashCode(Representative? value)
    {
        HashCalls++;
        if (value is null)
            return 0;
        var remainder = value.EquivalenceClass % _hashBuckets;
        return remainder < 0 ? remainder + _hashBuckets : remainder;
    }

    internal void ResetCounts()
    {
        HashCalls = 0;
        EqualityCalls = 0;
    }
}

internal sealed class SwitchableRepresentativeComparer(int hashBuckets = 1)
    : RepresentativeComparer(hashBuckets)
{
    internal ComparerCallbackException Failure { get; } = new();

    internal bool ThrowFromHash { get; set; }

    internal bool ThrowFromEquals { get; set; }

    public override bool Equals(Representative? left, Representative? right)
    {
        if (ThrowFromEquals)
            throw Failure;
        return base.Equals(left, right);
    }

    public override int GetHashCode(Representative? value)
    {
        if (ThrowFromHash)
            throw Failure;
        return base.GetHashCode(value);
    }
}

internal sealed class ReferenceRepresentativeComparer : IEqualityComparer<Representative?>
{
    internal ComparerCallbackException Failure { get; } = new();

    internal bool ThrowFromHash { get; set; }

    internal bool ThrowFromEquals { get; set; }

    public bool Equals(Representative? left, Representative? right)
    {
        if (ThrowFromEquals)
            throw Failure;
        return ReferenceEquals(left, right);
    }

    public int GetHashCode(Representative? value)
    {
        if (ThrowFromHash)
            throw Failure;
        return value is null ? 0 : System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(value);
    }
}

internal sealed class ComparerCallbackException : Exception
{
}

internal sealed class EnumerationCallbackException : Exception
{
}

internal sealed class OrderingCallbackException : Exception
{
}

internal static class OrderedSetAssert
{
    internal static void Matches<T>(IReadOnlyList<T> expected, PersistentOrderedSet<T> actual)
    {
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected.Count == 0, actual.IsEmpty);
        AssertReferenceSequence(expected, actual.ToArray());
        AssertReferenceSequence(expected, actual);

        for (var index = 0; index < expected.Count; index++)
        {
            AssertRepresentative(expected[index], actual[index]);
            AssertRepresentative(expected[index], actual.GetAt(index));
            Assert.Equal(index, actual.IndexOf(expected[index]));
            Assert.True(actual.Contains(expected[index]));
            Assert.True(actual.TryGetValue(expected[index], out var stored));
            AssertRepresentative(expected[index], stored);
        }

        if (expected.Count == 0)
        {
            Assert.Throws<InvalidOperationException>(() => actual.First);
            Assert.Throws<InvalidOperationException>(() => actual.Last);
        }
        else
        {
            AssertRepresentative(expected[0], actual.First);
            AssertRepresentative(expected[^1], actual.Last);
        }

        actual.ValidateInvariants();
    }

    internal static void AssertReferenceSequence<T>(IEnumerable<T> expected, IEnumerable<T> actual)
    {
        var expectedArray = expected.ToArray();
        var actualArray = actual.ToArray();
        Assert.Equal(expectedArray.Length, actualArray.Length);
        for (var index = 0; index < expectedArray.Length; index++)
            AssertRepresentative(expectedArray[index], actualArray[index]);
    }

    internal static void AssertRepresentative<T>(T expected, T actual)
    {
        if (typeof(T).IsValueType)
            Assert.Equal(expected, actual);
        else
            Assert.True(ReferenceEquals(expected, actual), "Expected the exact retained representative object.");
    }
}

internal sealed class OrderedListModel<T>(IEqualityComparer<T> comparer)
{
    private readonly List<T> _items = [];

    internal IEqualityComparer<T> Comparer { get; } = comparer;

    internal int Count => _items.Count;

    internal T this[int index] => _items[index];

    internal IReadOnlyList<T> Items => _items;

    internal OrderedListModel<T> Clone()
    {
        var clone = new OrderedListModel<T>(Comparer);
        clone._items.AddRange(_items);
        return clone;
    }

    internal bool Add(T item)
    {
        if (FindIndex(item) >= 0)
            return false;
        _items.Add(item);
        return true;
    }

    internal bool AddFirst(T item)
    {
        if (FindIndex(item) >= 0)
            return false;
        _items.Insert(0, item);
        return true;
    }

    internal bool Insert(int index, T item)
    {
        if (FindIndex(item) >= 0)
            return false;
        _items.Insert(index, item);
        return true;
    }

    internal bool MoveTo(int finalIndex, T equalValue)
    {
        var oldIndex = FindIndex(equalValue);
        if (oldIndex < 0)
            throw new KeyNotFoundException();
        if (oldIndex == finalIndex)
            return false;
        var stored = _items[oldIndex];
        _items.RemoveAt(oldIndex);
        _items.Insert(finalIndex, stored);
        return true;
    }

    internal bool Remove(T equalValue)
    {
        var index = FindIndex(equalValue);
        if (index < 0)
            return false;
        _items.RemoveAt(index);
        return true;
    }

    internal void RemoveAt(int index) => _items.RemoveAt(index);

    internal void GetRange(int index, int count)
    {
        var kept = _items.GetRange(index, count);
        _items.Clear();
        _items.AddRange(kept);
    }

    internal void Reverse() => _items.Reverse();

    internal void StableSort(IComparer<T> comparer)
    {
        var sorted = _items
            .Select((item, index) => (Item: item, Index: index))
            .OrderBy(pair => pair.Item, comparer)
            .ThenBy(pair => pair.Index)
            .Select(pair => pair.Item)
            .ToArray();
        _items.Clear();
        _items.AddRange(sorted);
    }

    internal void Clear() => _items.Clear();

    internal void Union(IEnumerable<T> argument)
    {
        foreach (var item in Normalize(argument))
            Add(item);
    }

    internal void Except(IEnumerable<T> argument)
    {
        foreach (var item in Normalize(argument))
            Remove(item);
    }

    internal void Intersect(IEnumerable<T> argument)
    {
        var normalized = Normalize(argument);
        _items.RemoveAll(item => !normalized.Any(candidate => Comparer.Equals(item, candidate)));
    }

    internal void SymmetricExcept(IEnumerable<T> argument)
    {
        var normalized = Normalize(argument);
        var receiverOnly = _items
            .Where(item => !normalized.Any(candidate => Comparer.Equals(item, candidate)))
            .ToArray();
        var argumentOnly = normalized
            .Where(item => !_items.Any(candidate => Comparer.Equals(item, candidate)))
            .ToArray();
        _items.Clear();
        _items.AddRange(receiverOnly);
        _items.AddRange(argumentOnly);
    }

    internal T[] Normalize(IEnumerable<T> argument)
    {
        List<T> result = [];
        foreach (var item in argument)
        {
            if (!result.Any(existing => Comparer.Equals(existing, item)))
                result.Add(item);
        }
        return result.ToArray();
    }

    internal int FindIndex(T equalValue)
    {
        for (var index = 0; index < _items.Count; index++)
        {
            if (Comparer.Equals(_items[index], equalValue))
                return index;
        }
        return -1;
    }
}
