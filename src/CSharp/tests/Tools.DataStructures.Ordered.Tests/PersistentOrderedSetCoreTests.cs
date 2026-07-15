using Xunit;

namespace Tools.DataStructures.Ordered.Tests;

/// <summary>Construction, lookup, addition, removal, representative, and empty-state tests.</summary>
public sealed class PersistentOrderedSetCoreTests
{
    /// <summary>Verifies only the reference-default comparer selects the shared empty singleton.</summary>
    [Fact]
    public void EmptyFactories_CanonicalizeOnlyTheReferenceDefaultComparer()
    {
        var defaultComparer = EqualityComparer<string>.Default;
        Assert.Same(PersistentOrderedSet<string>.Empty, PersistentOrderedSet<string>.Empty);
        Assert.Same(PersistentOrderedSet<string>.Empty, PersistentOrderedSet<string>.Create());
        Assert.Same(PersistentOrderedSet<string>.Empty, PersistentOrderedSet<string>.Create(defaultComparer));
        Assert.Same(PersistentOrderedSet<string>.Empty, PersistentOrderedSet<string>.CreateRange([]));
        Assert.Same(
            PersistentOrderedSet<string>.Empty,
            PersistentOrderedSet<string>.CreateRange([], defaultComparer));

        var customComparer = new RepresentativeComparer();
        var custom = PersistentOrderedSet<Representative?>.Create(customComparer);
        var customRange = PersistentOrderedSet<Representative?>.CreateRange([], customComparer);
        Assert.NotSame(PersistentOrderedSet<Representative?>.Empty, custom);
        Assert.NotSame(PersistentOrderedSet<Representative?>.Empty, customRange);
        Assert.Same(customComparer, custom.Comparer);
        Assert.Same(customComparer, customRange.Comparer);
        Assert.True(custom.IsEmpty);
        Assert.Empty(custom);
        custom.ValidateInvariants();
        customRange.ValidateInvariants();
    }

    /// <summary>Verifies bulk construction retains the first representative and first position of every class.</summary>
    [Fact]
    public void CreateRange_CollapsesDuplicatesAtTheirFirstPositions()
    {
        var comparer = new RepresentativeComparer();
        var firstAlpha = new Representative(1, "first-alpha");
        var beta = new Representative(2, "beta");
        var laterAlpha = new Representative(1, "later-alpha");
        var gamma = new Representative(3, "gamma");
        var latestAlpha = new Representative(1, "latest-alpha");

        var set = PersistentOrderedSet<Representative>.CreateRange(
            [firstAlpha, beta, laterAlpha, gamma, latestAlpha],
            comparer);

        OrderedSetAssert.Matches(new[] { firstAlpha, beta, gamma }, set);
        Assert.True(set.TryGetValue(laterAlpha, out var actual));
        Assert.Same(firstAlpha, actual);
        Assert.Equal(0, set.IndexOf(latestAlpha));
        Assert.Same(comparer, set.Comparer);
    }

    /// <summary>Verifies null is an ordinary comparer-governed equivalence class.</summary>
    [Fact]
    public void NullRepresentative_IsConstructedLookedUpMovedAndRemovedNormally()
    {
        var comparer = new RepresentativeComparer();
        var item = new Representative(1, "item");
        var set = PersistentOrderedSet<Representative?>.CreateRange([null, item, null], comparer);

        OrderedSetAssert.Matches<Representative?>([null, item], set);
        Assert.True(set.Contains(null));
        Assert.True(set.TryGetValue(null, out var storedNull));
        Assert.Null(storedNull);

        var moved = set.MoveToLast(null);
        OrderedSetAssert.Matches<Representative?>([item, null], moved);
        var removed = moved.Remove(null);
        OrderedSetAssert.Matches<Representative?>([item], removed);
        OrderedSetAssert.Matches<Representative?>([null, item], set);
    }

    /// <summary>Verifies lookup misses echo the supplied lookup representative and report no position.</summary>
    [Fact]
    public void LookupMiss_EchoesLookupArgumentAndReturnsMinusOnePosition()
    {
        var comparer = new RepresentativeComparer();
        var stored = new Representative(1, "stored");
        var missing = new Representative(2, "missing");
        var set = PersistentOrderedSet<Representative>.Create(comparer).Add(stored);

        Assert.False(set.Contains(missing));
        Assert.False(set.TryGetValue(missing, out var echoed));
        Assert.Same(missing, echoed);
        Assert.Equal(-1, set.IndexOf(missing));
        Assert.Same(stored, set[0]);
        Assert.Same(stored, set.GetAt(0));
    }

    /// <summary>Verifies duplicate additions never move or replace an existing representative.</summary>
    [Fact]
    public void AddVariants_SeparateInsertionFromMovementAndRetainRepresentatives()
    {
        var comparer = new RepresentativeComparer();
        var first = new Representative(1, "first");
        var middle = new Representative(2, "middle");
        var last = new Representative(3, "last");
        var equivalentMiddle = new Representative(2, "equivalent-middle");
        var source = PersistentOrderedSet<Representative>.CreateRange([first, middle, last], comparer);

        Assert.Same(source, source.Add(equivalentMiddle));
        Assert.Same(source, source.AddFirst(equivalentMiddle));
        Assert.Same(source, source.Insert(0, equivalentMiddle));
        Assert.Same(source, source.Insert(source.Count, equivalentMiddle));
        OrderedSetAssert.Matches(new[] { first, middle, last }, source);

        var appended = new Representative(4, "appended");
        var prepended = new Representative(5, "prepended");
        var inserted = new Representative(6, "inserted");
        OrderedSetAssert.Matches(new[] { first, middle, last, appended }, source.Add(appended));
        OrderedSetAssert.Matches(new[] { prepended, first, middle, last }, source.AddFirst(prepended));
        OrderedSetAssert.Matches(new[] { first, inserted, middle, last }, source.Insert(1, inserted));
        OrderedSetAssert.Matches(new[] { first, middle, last }, source);
    }

    /// <summary>Verifies all removal shapes, their no-op identities, and retained source snapshots.</summary>
    [Fact]
    public void RemovalVariants_PreserveOrderIdentityAndOldVersions()
    {
        var comparer = new RepresentativeComparer();
        var first = new Representative(1, "first");
        var middle = new Representative(2, "middle");
        var last = new Representative(3, "last");
        var source = PersistentOrderedSet<Representative>.CreateRange([first, middle, last], comparer);
        var missing = new Representative(9, "missing");

        Assert.Same(source, source.Remove(missing));
        Assert.False(source.TryRemove(missing, out var unchanged));
        Assert.Same(source, unchanged);

        Assert.True(source.TryRemove(new Representative(2, "lookup"), out var byTry));
        OrderedSetAssert.Matches(new[] { first, last }, byTry);
        OrderedSetAssert.Matches(new[] { first, last }, source.Remove(middle));
        OrderedSetAssert.Matches(new[] { first, last }, source.RemoveAt(1));
        OrderedSetAssert.Matches(new[] { middle, last }, source.RemoveFirst());
        OrderedSetAssert.Matches(new[] { first, middle }, source.RemoveLast());
        OrderedSetAssert.Matches(new[] { first, middle, last }, source);
    }

    /// <summary>Verifies clear preserves comparer identity and canonicalizes only default-comparer emptiness.</summary>
    [Fact]
    public void Clear_PreservesComparerAndEmptyIdentityRules()
    {
        var comparer = new RepresentativeComparer();
        var custom = PersistentOrderedSet<Representative>.Create(comparer)
            .Add(new Representative(1, "value"));
        var customEmpty = custom.Clear();
        Assert.Same(comparer, customEmpty.Comparer);
        Assert.NotSame(PersistentOrderedSet<Representative>.Empty, customEmpty);
        Assert.Same(customEmpty, customEmpty.Clear());

        var defaultEmpty = PersistentOrderedSet<int>.Empty.Add(1).Clear();
        Assert.Same(PersistentOrderedSet<int>.Empty, defaultEmpty);
        OrderedSetAssert.Matches(Array.Empty<int>(), defaultEmpty);
    }

    /// <summary>Verifies documented empty-state and positional argument failures.</summary>
    [Fact]
    public void EmptyAndInvalidPositions_ThrowTheirOwnedExceptions()
    {
        var empty = PersistentOrderedSet<int>.Empty;
        Assert.Throws<InvalidOperationException>(() => empty.First);
        Assert.Throws<InvalidOperationException>(() => empty.Last);
        Assert.Throws<InvalidOperationException>(empty.RemoveFirst);
        Assert.Throws<InvalidOperationException>(empty.RemoveLast);
        Assert.Throws<KeyNotFoundException>(() => empty.MoveToFirst(1));
        Assert.Throws<KeyNotFoundException>(() => empty.MoveToLast(1));
        Assert.Throws<ArgumentOutOfRangeException>("index", () => empty.MoveTo(0, 1));

        var set = PersistentOrderedSet<int>.CreateRange([1, 2, 3]);
        Assert.Throws<ArgumentOutOfRangeException>("index", () => set[-1]);
        Assert.Throws<ArgumentOutOfRangeException>("index", () => set[3]);
        Assert.Throws<ArgumentOutOfRangeException>("index", () => set.GetAt(3));
        Assert.Throws<ArgumentOutOfRangeException>("index", () => set.RemoveAt(-1));
        Assert.Throws<ArgumentOutOfRangeException>("index", () => set.Insert(4, 4));
        Assert.Throws<ArgumentOutOfRangeException>("index", () => set.MoveTo(3, 1));
        OrderedSetAssert.Matches(new[] { 1, 2, 3 }, set);
    }

    /// <summary>Verifies null construction sources are rejected and enumerator failures propagate exactly.</summary>
    [Fact]
    public void CreateRange_RejectsNullAndPropagatesEnumerationFailure()
    {
        Assert.Throws<ArgumentNullException>(() => PersistentOrderedSet<int>.CreateRange(null!));
        var failure = new EnumerationCallbackException();
        var actual = Assert.Throws<EnumerationCallbackException>(
            () => PersistentOrderedSet<int>.CreateRange(ThrowAfterTwo(failure)));
        Assert.Same(failure, actual);
    }

    private static IEnumerable<int> ThrowAfterTwo(Exception failure)
    {
        yield return 1;
        yield return 2;
        throw failure;
    }
}
