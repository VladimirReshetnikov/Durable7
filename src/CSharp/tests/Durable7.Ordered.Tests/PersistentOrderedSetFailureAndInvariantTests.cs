// Tests for the persistent ordered set failure and invariant.

using System.Reflection;
using Xunit;

namespace Durable7.Ordered.Tests;

/// <summary>Failure atomicity, validation precedence, rebuild, and invariant-diagnostic tests.</summary>
public sealed class PersistentOrderedSetFailureAndInvariantTests
{
    /// <summary>Verifies positional validation occurs before any equality-comparer callback.</summary>
    [Fact]
    public void InvalidPositions_PrecedeHashAndEqualityCallbacks()
    {
        var comparer = new SwitchableRepresentativeComparer();
        var value = new Representative(1, "value");
        var source = PersistentOrderedSet<Representative>.Create(comparer).Add(value);
        comparer.ResetCounts();
        comparer.ThrowFromHash = true;
        comparer.ThrowFromEquals = true;

        Assert.Throws<ArgumentOutOfRangeException>("index", () => source.Insert(-1, value));
        Assert.Throws<ArgumentOutOfRangeException>("index", () => source.Insert(2, value));
        Assert.Throws<ArgumentOutOfRangeException>("index", () => source.MoveTo(-1, value));
        Assert.Throws<ArgumentOutOfRangeException>("index", () => source.MoveTo(1, value));
        Assert.Throws<ArgumentOutOfRangeException>("index", () => source.GetRange(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>("count", () => source.GetRange(0, 2));
        Assert.Throws<ArgumentOutOfRangeException>("count", () => source.Take(2));
        Assert.Throws<ArgumentOutOfRangeException>("count", () => source.Drop(2));
        Assert.Equal(0, comparer.HashCalls);
        Assert.Equal(0, comparer.EqualityCalls);

        comparer.ThrowFromHash = false;
        comparer.ThrowFromEquals = false;
        OrderedSetAssert.Matches(new[] { value }, source);
    }

    /// <summary>Verifies hash failures leave point-update sources and retained representatives unchanged.</summary>
    [Fact]
    public void PointOperationHashFailures_AreFailureAtomic()
    {
        var comparer = new SwitchableRepresentativeComparer();
        var first = new Representative(1, "first");
        var second = new Representative(2, "second");
        var source = PersistentOrderedSet<Representative>.CreateRange([first, second], comparer);
        var expected = source.ToArray();
        comparer.ThrowFromHash = true;

        foreach (var operation in new Func<object?>[]
                 {
                     () => source.Add(new Representative(3, "new")),
                     () => source.AddFirst(new Representative(3, "new")),
                     () => source.Insert(1, new Representative(3, "new")),
                     () => source.Remove(new Representative(1, "lookup")),
                     () => source.MoveToFirst(new Representative(2, "lookup")),
                     () => source.MoveToLast(new Representative(1, "lookup")),
                     () => source.IndexOf(new Representative(1, "lookup")),
                     () => source.Contains(new Representative(1, "lookup")),
                 })
        {
            var actual = Assert.Throws<ComparerCallbackException>(operation);
            Assert.Same(comparer.Failure, actual);
        }

        comparer.ThrowFromHash = false;
        OrderedSetAssert.Matches(expected, source);
    }

    /// <summary>Verifies collision-bucket equality failures leave the source unchanged.</summary>
    [Fact]
    public void CollisionEqualityFailures_AreFailureAtomic()
    {
        var comparer = new SwitchableRepresentativeComparer();
        var first = new Representative(1, "first");
        var second = new Representative(2, "second");
        var source = PersistentOrderedSet<Representative>.CreateRange([first, second], comparer);
        var expected = source.ToArray();
        comparer.ThrowFromEquals = true;
        var missing = new Representative(99, "missing");

        foreach (var operation in new Func<object?>[]
                 {
                     () => source.Contains(missing),
                     () => source.IndexOf(missing),
                     () => source.Add(missing),
                     () => source.Remove(missing),
                     () => source.MoveToFirst(missing),
                 })
        {
            var actual = Assert.Throws<ComparerCallbackException>(operation);
            Assert.Same(comparer.Failure, actual);
        }

        comparer.ThrowFromEquals = false;
        OrderedSetAssert.Matches(expected, source);
    }

    /// <summary>Verifies failures from both range reconciliation strategies and reverse rebuild are atomic.</summary>
    [Fact]
    public void DerivedIndexRebuildFailures_LeaveTheSourceUnchanged()
    {
        var comparer = new SwitchableRepresentativeComparer(hashBuckets: 4);
        var items = Enumerable.Range(0, 12)
            .Select(index => new Representative(index, $"item-{index}"))
            .ToArray();
        var source = PersistentOrderedSet<Representative>.CreateRange(items, comparer);
        comparer.ThrowFromHash = true;

        foreach (var operation in new Func<object?>[]
                 {
                     () => source.GetRange(6, 1),
                     () => source.GetRange(1, 10),
                     source.Reverse,
                 })
        {
            var actual = Assert.Throws<ComparerCallbackException>(operation);
            Assert.Same(comparer.Failure, actual);
        }

        comparer.ThrowFromHash = false;
        OrderedSetAssert.Matches(items, source);
    }

    /// <summary>Verifies ordering comparer failure and subsequent index-build failure cannot publish partial sorts.</summary>
    [Fact]
    public void SortFailures_LeaveTheSourceUnchanged()
    {
        var comparer = new SwitchableRepresentativeComparer();
        var items = new[]
        {
            new Representative(3, "three"),
            new Representative(1, "one"),
            new Representative(2, "two"),
        };
        var source = PersistentOrderedSet<Representative>.CreateRange(items, comparer);
        var orderFailure = new OrderingCallbackException();
        var throwingOrder = Comparer<Representative>.Create((_, _) => throw orderFailure);
        var sortFailure = Assert.Throws<InvalidOperationException>(() => source.Sort(throwingOrder));
        Assert.Same(orderFailure, sortFailure.InnerException);
        OrderedSetAssert.Matches(items, source);

        comparer.ThrowFromHash = true;
        var validOrder = Comparer<Representative>.Create(
            (left, right) => left.EquivalenceClass.CompareTo(right.EquivalenceClass));
        var actualHashFailure = Assert.Throws<ComparerCallbackException>(() => source.Sort(validOrder));
        Assert.Same(comparer.Failure, actualHashFailure);
        comparer.ThrowFromHash = false;
        OrderedSetAssert.Matches(items, source);

        var empty = PersistentOrderedSet<Representative>.Create(comparer);
        var single = empty.Add(items[0]);
        Assert.Same(empty, empty.Sort(throwingOrder));
        Assert.Same(single, single.Sort(throwingOrder));
    }

    /// <summary>Verifies private stamp and stable-order comparisons tolerate extreme comparison results.</summary>
    [Fact]
    public void SortComparer_ExtremeResultsDoNotOverflowOrdering()
    {
        var source = PersistentOrderedSet<int>.CreateRange([3, 1, 4, 2]);
        var extreme = Comparer<int>.Create((left, right) => left == right ? 0 : left < right ? int.MinValue : int.MaxValue);
        OrderedSetAssert.Matches(new[] { 1, 2, 3, 4 }, source.Sort(extreme));
        OrderedSetAssert.Matches(new[] { 3, 1, 4, 2 }, source);
    }

    /// <summary>Verifies clear and trivial reverse/sort shortcuts do not invoke a failing comparer.</summary>
    [Fact]
    public void StructuralIdentityShortcuts_BypassUserCallbacks()
    {
        var comparer = new SwitchableRepresentativeComparer();
        var item = new Representative(1, "item");
        var single = PersistentOrderedSet<Representative>.Create(comparer).Add(item);
        var empty = PersistentOrderedSet<Representative>.Create(comparer);
        comparer.ThrowFromHash = true;
        comparer.ThrowFromEquals = true;
        var throwingOrder = Comparer<Representative>.Create((_, _) => throw new OrderingCallbackException());

        Assert.Same(empty, empty.Clear());
        Assert.Same(empty, empty.Reverse());
        Assert.Same(single, single.Reverse());
        Assert.Same(empty, empty.Sort(throwingOrder));
        Assert.Same(single, single.Sort(throwingOrder));
        var cleared = single.Clear();
        Assert.True(cleared.IsEmpty);
        Assert.Same(comparer, cleared.Comparer);

        comparer.ThrowFromHash = false;
        comparer.ThrowFromEquals = false;
        OrderedSetAssert.Matches(new[] { item }, single);
        OrderedSetAssert.Matches(Array.Empty<Representative>(), cleared);
    }

    /// <summary>Verifies invariant diagnostics detect an exact-stamp disagreement between the two indexes.</summary>
    [Fact]
    public void InvariantDiagnostics_RejectStampDisagreement()
    {
        var source = PersistentOrderedSet<int>.CreateRange([1, 2, 3]);
        var differentStamps = source.Reverse();
        var malformed = CombineOrderAndStamps(source, differentStamps);
        var failure = Assert.Throws<InvalidOperationException>(malformed.ValidateInvariants);
        Assert.Contains("stamp", failure.Message, StringComparison.OrdinalIgnoreCase);
        OrderedSetAssert.Matches(new[] { 1, 2, 3 }, source);
        OrderedSetAssert.Matches(new[] { 3, 2, 1 }, differentStamps);
    }

    /// <summary>Verifies invariant diagnostics reject map/order representative disagreement even when classes match.</summary>
    [Fact]
    public void InvariantDiagnostics_RejectRepresentativeDisagreement()
    {
        var comparer = new RepresentativeComparer();
        var firstA = new Representative(1, "first-a");
        var firstB = new Representative(2, "first-b");
        var otherA = new Representative(1, "other-a");
        var otherB = new Representative(2, "other-b");
        var orderOwner = PersistentOrderedSet<Representative>.CreateRange([firstA, firstB], comparer);
        var stampOwner = PersistentOrderedSet<Representative>.CreateRange([otherA, otherB], comparer);
        var malformed = CombineOrderAndStamps(orderOwner, stampOwner);
        var failure = Assert.Throws<InvalidOperationException>(malformed.ValidateInvariants);
        Assert.Contains("representative", failure.Message, StringComparison.OrdinalIgnoreCase);
        OrderedSetAssert.Matches(new[] { firstA, firstB }, orderOwner);
        OrderedSetAssert.Matches(new[] { otherA, otherB }, stampOwner);
    }

    private static PersistentOrderedSet<T> CombineOrderAndStamps<T>(
        PersistentOrderedSet<T> orderOwner,
        PersistentOrderedSet<T> stampOwner)
    {
        const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic;
        var type = typeof(PersistentOrderedSet<T>);
        var orderField = type.GetField("_order", flags)!;
        var stampsField = type.GetField("_stamps", flags)!;
        var constructor = type.GetConstructor(
            flags,
            binder: null,
            [orderField.FieldType, stampsField.FieldType],
            modifiers: null)!;
        return (PersistentOrderedSet<T>)constructor.Invoke(
            [orderField.GetValue(orderOwner), stampsField.GetValue(stampOwner)]);
    }
}
