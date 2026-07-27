// Shared support for the ordered set tests.

using Xunit;

namespace Durable7.Ordered.Tests;

/// <summary>
/// A test value whose identity is distinguishable from its equality, so a test can tell which of
/// two equal values a collection actually stored.
/// </summary>
internal sealed class Representative(int equivalenceClass, string name)
{
    /// <summary>Gets the equivalence class.</summary>
    internal int EquivalenceClass { get; } = equivalenceClass;

    /// <summary>Gets the name.</summary>
    internal string Name { get; } = name;

    /// <summary>Returns a readable representation, for diagnostics.</summary>
    public override string ToString() => $"{Name}:{EquivalenceClass}";
}

/// <summary>
/// Equality over test representatives by their comparison key, ignoring their identity.
/// </summary>
internal class RepresentativeComparer(int hashBuckets = 1) : IEqualityComparer<Representative?>
{
    private readonly int _hashBuckets = hashBuckets > 0
        ? hashBuckets
        : throw new ArgumentOutOfRangeException(nameof(hashBuckets));

    /// <summary>
    /// Gets how many times the policy was asked to hash, so a test can assert an operation consulted it only as often
    /// as its bound allows.
    /// </summary>
    internal int HashCalls { get; private set; }

    /// <summary>Gets how many times the policy was asked to compare.</summary>
    internal int EqualityCalls { get; private set; }

    /// <summary>Determines whether both values hold the same elements.</summary>
    public virtual bool Equals(Representative? left, Representative? right)
    {
        EqualityCalls++;
        return left?.EquivalenceClass == right?.EquivalenceClass;
    }

    /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
    public virtual int GetHashCode(Representative? value)
    {
        HashCalls++;
        if (value is null)
            return 0;
        var remainder = value.EquivalenceClass % _hashBuckets;
        return remainder < 0 ? remainder + _hashBuckets : remainder;
    }

    /// <summary>Clears the recorded counts.</summary>
    internal void ResetCounts()
    {
        HashCalls = 0;
        EqualityCalls = 0;
    }
}

/// <summary>
/// A representative comparer whose behaviour can be changed mid-test, so a test can check what a
/// collection does when its policy stops agreeing with itself.
/// </summary>
internal sealed class SwitchableRepresentativeComparer(int hashBuckets = 1)
    : RepresentativeComparer(hashBuckets)
{
    internal ComparerCallbackException Failure { get; } = new();

    internal bool ThrowFromHash { get; set; }

    internal int? ThrowOnHashCall { get; set; }

    internal bool ThrowFromEquals { get; set; }

    public override bool Equals(Representative? left, Representative? right)
    {
        if (ThrowFromEquals)
            throw Failure;
        return base.Equals(left, right);
    }

    public override int GetHashCode(Representative? value)
    {
        if (ThrowFromHash || ThrowOnHashCall == HashCalls + 1)
            throw Failure;
        return base.GetHashCode(value);
    }
}

/// <summary>
/// Equality over test representatives by reference, so a test can assert which instance was
/// retained.
/// </summary>
internal sealed class ReferenceRepresentativeComparer : IEqualityComparer<Representative?>
{
    /// <summary>Gets the failure this result carries.</summary>
    internal ComparerCallbackException Failure { get; } = new();

    /// <summary>
    /// Throws from the hash callback on demand, so a test can check that a failing callback leaves the collection
    /// unchanged.
    /// </summary>
    internal bool ThrowFromHash { get; set; }

    /// <summary>
    /// Throws from the equality callback on demand, so a test can check that a failing comparison leaves the collection
    /// unchanged.
    /// </summary>
    internal bool ThrowFromEquals { get; set; }

    /// <summary>Determines whether both values hold the same elements.</summary>
    public bool Equals(Representative? left, Representative? right)
    {
        if (ThrowFromEquals)
            throw Failure;
        return ReferenceEquals(left, right);
    }

    /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
    public int GetHashCode(Representative? value)
    {
        if (ThrowFromHash)
            throw Failure;
        return value is null ? 0 : System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(value);
    }
}

/// <summary>
/// Thrown from a test comparer on demand, to check that a failing comparison leaves the collection
/// unchanged.
/// </summary>
internal sealed class ComparerCallbackException : Exception
{
}

/// <summary>
/// Thrown from a test enumerator on demand, to check that a failing enumeration leaves the
/// collection unchanged.
/// </summary>
internal sealed class EnumerationCallbackException : Exception
{
}

/// <summary>
/// Thrown from a test ordering callback on demand, to check that a failing ordering leaves the
/// collection unchanged.
/// </summary>
internal sealed class OrderingCallbackException : Exception
{
}

/// <summary>
/// Assertions about an insertion-ordered set's contents, order, and retained representatives.
/// </summary>
internal static class OrderedSetAssert
{
    /// <summary>Asserts that the structure matches the model it is compared against.</summary>
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

    /// <summary>Asserts the reference sequence.</summary>
    internal static void AssertReferenceSequence<T>(IEnumerable<T> expected, IEnumerable<T> actual)
    {
        var expectedArray = expected.ToArray();
        var actualArray = actual.ToArray();
        Assert.Equal(expectedArray.Length, actualArray.Length);
        for (var index = 0; index < expectedArray.Length; index++)
            AssertRepresentative(expectedArray[index], actualArray[index]);
    }

    /// <summary>Asserts the representative.</summary>
    internal static void AssertRepresentative<T>(T expected, T actual)
    {
        if (typeof(T).IsValueType)
            Assert.Equal(expected, actual);
        else
            Assert.True(ReferenceEquals(expected, actual), "Expected the exact retained representative object.");
    }
}

/// <summary>
/// A plain list model the insertion-ordered set is compared against. Deliberately naive: it is
/// right by inspection, which is what makes a disagreement evidence against the real structure.
/// </summary>
internal sealed class OrderedListModel<T>(IEqualityComparer<T> comparer)
{
    private readonly List<T> _items = [];

    /// <summary>Gets the retained ordering policy.</summary>
    internal IEqualityComparer<T> Comparer { get; } = comparer;

    /// <summary>Gets the number of elements in the collection.</summary>
    internal int Count => _items.Count;

    internal T this[int index] => _items[index];

    /// <summary>Gets the elements.</summary>
    internal IReadOnlyList<T> Items => _items;

    /// <summary>Returns a second handle on the same collection version.</summary>
    internal OrderedListModel<T> Clone()
    {
        var clone = new OrderedListModel<T>(Comparer);
        clone._items.AddRange(_items);
        return clone;
    }

    /// <summary>Returns a collection containing the given element.</summary>
    internal bool Add(T item)
    {
        if (FindIndex(item) >= 0)
            return false;
        _items.Add(item);
        return true;
    }

    /// <summary>Adds the first.</summary>
    internal bool AddFirst(T item)
    {
        if (FindIndex(item) >= 0)
            return false;
        _items.Insert(0, item);
        return true;
    }

    /// <summary>Returns a collection with the element inserted.</summary>
    internal bool Insert(int index, T item)
    {
        if (FindIndex(item) >= 0)
            return false;
        _items.Insert(index, item);
        return true;
    }

    /// <summary>Moves the to.</summary>
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

    /// <summary>Returns a collection without that element.</summary>
    internal bool Remove(T equalValue)
    {
        var index = FindIndex(equalValue);
        if (index < 0)
            return false;
        _items.RemoveAt(index);
        return true;
    }

    /// <summary>Removes the at.</summary>
    internal void RemoveAt(int index) => _items.RemoveAt(index);

    /// <summary>Returns the range.</summary>
    internal void GetRange(int index, int count)
    {
        var kept = _items.GetRange(index, count);
        _items.Clear();
        _items.AddRange(kept);
    }

    /// <summary>Returns the model in the opposite order.</summary>
    internal void Reverse() => _items.Reverse();

    /// <summary>Returns the model sorted, keeping equal elements in their original order.</summary>
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

    /// <summary>Returns an empty collection retaining the same policies.</summary>
    internal void Clear() => _items.Clear();

    /// <summary>Returns the elements of both models.</summary>
    internal void Union(IEnumerable<T> argument)
    {
        foreach (var item in Normalize(argument))
            Add(item);
    }

    /// <summary>Returns this model's elements that are absent from the other.</summary>
    internal void Except(IEnumerable<T> argument)
    {
        foreach (var item in Normalize(argument))
            Remove(item);
    }

    /// <summary>Returns the elements present in both models.</summary>
    internal void Intersect(IEnumerable<T> argument)
    {
        var normalized = Normalize(argument);
        _items.RemoveAll(item => !normalized.Any(candidate => Comparer.Equals(item, candidate)));
    }

    /// <summary>Returns the elements present in exactly one of the two models.</summary>
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

    /// <summary>
    /// Returns the model in its canonical form, so two models built differently compare equal exactly when they should.
    /// </summary>
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

    /// <summary>Finds the index.</summary>
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
