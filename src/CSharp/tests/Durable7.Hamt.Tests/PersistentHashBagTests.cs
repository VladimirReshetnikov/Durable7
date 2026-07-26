// Tests for the persistent hash bag.

using System.Reflection;
using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Core construction, point-update, failure, overflow, and diagnostic tests for persistent hash bags.</summary>
public sealed class PersistentHashBagTests
{
    /// <summary>Verifies only the reference-default comparer selects the shared empty singleton.</summary>
    [Fact]
    public void EmptyFactories_CanonicalizeOnlyTheReferenceDefaultComparer()
    {
        var defaultComparer = EqualityComparer<string>.Default;

        Assert.Same(PersistentHashBag<string>.Empty, PersistentHashBag<string>.Create());
        Assert.Same(PersistentHashBag<string>.Empty, PersistentHashBag<string>.Create(defaultComparer));
        Assert.Same(PersistentHashBag<string>.Empty, PersistentHashBag<string>.CreateRange([]));
        Assert.Same(
            PersistentHashBag<string>.Empty,
            PersistentHashBag<string>.CreateRange([], defaultComparer));

        var equivalentComparer = new OrdinalComparer();
        var custom = PersistentHashBag<string>.Create(equivalentComparer);
        var customRange = PersistentHashBag<string>.CreateRange([], equivalentComparer);

        Assert.NotSame(PersistentHashBag<string>.Empty, custom);
        Assert.NotSame(PersistentHashBag<string>.Empty, customRange);
        Assert.Same(equivalentComparer, custom.Comparer);
        Assert.Same(equivalentComparer, customRange.Comparer);
        Assert.True(custom.IsEmpty);
        Assert.Equal(0, custom.DistinctCount);
        Assert.Equal(0, custom.TotalCount);
    }

    /// <summary>Verifies bulk construction hashes once per occurrence and retains first representatives.</summary>
    [Fact]
    public void CreateRange_AggregatesOccurrencesAndRetainsFirstRepresentatives()
    {
        var comparer = new CountingConstantIgnoreCaseComparer();
        var storedAlpha = NewString("Alpha");
        var equivalentAlpha = NewString("ALPHA");
        var beta = NewString("Beta");
        var items = new[] { storedAlpha, equivalentAlpha, beta, storedAlpha };

        var bag = PersistentHashBag<string>.CreateRange(items, comparer);

        Assert.Equal(items.Length, comparer.HashCalls);
        Assert.Equal(2, bag.DistinctCount);
        Assert.Equal(4, bag.TotalCount);
        Assert.Equal(3, bag.CountOf("alpha"));
        Assert.Equal(1, bag.CountOf("BETA"));
        Assert.True(bag.Contains("aLpHa"));
        Assert.True(bag.TryGetValue(equivalentAlpha, out var actualAlpha));
        Assert.Same(storedAlpha, actualAlpha);
        Assert.Same(beta, bag.Entries.Single(entry => comparer.Equals(entry.Key, "beta")).Key);
        Assert.Equal(4, bag.ToArray().Length);

        var diagnostics = bag.ValidateCanonicalityForDiagnostics();
        Assert.Equal(2, diagnostics.DistinctCount);
        Assert.Equal(4, diagnostics.TotalCount);
    }

    /// <summary>Verifies null is an ordinary comparer-governed equivalence class.</summary>
    [Fact]
    public void NullItems_AreAggregatedLookedUpAndRemoved()
    {
        var bag = PersistentHashBag<string?>.CreateRange([null, "present", null]);

        Assert.True(bag.Contains(null));
        Assert.Equal(2, bag.CountOf(null));
        Assert.True(bag.TryGetValue(null, out var actualNull));
        Assert.Null(actualNull);
        Assert.Equal(2, bag.DistinctCount);
        Assert.Equal(3, bag.TotalCount);

        var oneNull = bag.Remove(null);
        var noNull = oneNull.RemoveAll(null);

        Assert.Equal(1, oneNull.CountOf(null));
        Assert.False(noNull.Contains(null));
        Assert.Equal(1, noNull.TotalCount);
        Assert.Equal(2, bag.CountOf(null));
    }

    /// <summary>Verifies a lookup miss echoes the caller's lookup representative.</summary>
    [Fact]
    public void TryGetValue_OnMissEchoesLookupArgument()
    {
        var stored = NewString("stored");
        var lookup = NewString("missing");
        var bag = PersistentHashBag<string>.Empty.Add(stored);

        Assert.False(bag.TryGetValue(lookup, out var actual));
        Assert.Same(lookup, actual);
        Assert.Equal(0, bag.CountOf(lookup));
        Assert.False(bag.Contains(lookup));
    }

    /// <summary>Verifies point updates saturate removals, preserve representatives, and retain old versions.</summary>
    [Fact]
    public void PointUpdates_PreserveRepresentativesIdentityAndSnapshots()
    {
        var stored = NewString("Alpha");
        var equivalent = NewString("ALPHA");
        var empty = PersistentHashBag<string>.Create(StringComparer.OrdinalIgnoreCase);
        var one = empty.Add(stored);
        var three = one.AddCopies(equivalent, 2);

        Assert.Equal(3, three.CountOf(equivalent));
        Assert.True(three.TryGetValue(equivalent, out var actual));
        Assert.Same(stored, actual);
        Assert.Same(three, three.AddCopies(equivalent, 0));
        Assert.Same(three, three.RemoveCopies(equivalent, 0));
        Assert.Same(three, three.Remove("missing"));
        Assert.Same(three, three.RemoveAll("missing"));

        var two = three.Remove(equivalent);
        var removed = two.RemoveCopies(equivalent, 100);

        Assert.Equal(2, two.CountOf(stored));
        Assert.True(removed.IsEmpty);
        Assert.Same(StringComparer.OrdinalIgnoreCase, removed.Comparer);
        Assert.Equal(0, removed.TotalCount);
        Assert.Equal(0, empty.TotalCount);
        Assert.Equal(1, one.TotalCount);
        Assert.Equal(3, three.TotalCount);
        Assert.Same(stored, Assert.Single(one));
        Assert.Equal(3, three.ToArray().Length);
    }

    /// <summary>Verifies copy-count validation and zero no-ops occur before comparer callbacks.</summary>
    [Fact]
    public void CopyCountValidation_PrecedesHashingAndZeroIsAnIdentityNoOp()
    {
        var comparer = new SwitchableThrowingComparer { ThrowFromGetHashCode = true };
        var bag = PersistentHashBag<string>.Create(comparer);

        Assert.Same(bag, bag.AddCopies("value", 0));
        Assert.Same(bag, bag.RemoveCopies("value", 0));

        var addFailure = Assert.Throws<ArgumentOutOfRangeException>(() => bag.AddCopies("value", -1));
        var removeFailure = Assert.Throws<ArgumentOutOfRangeException>(() => bag.RemoveCopies("value", -1));

        Assert.Equal("count", addFailure.ParamName);
        Assert.Equal("count", removeFailure.ParamName);
        Assert.Equal(0, comparer.HashCalls);
    }

    /// <summary>Verifies checked per-class limits and totals larger than <see cref="int.MaxValue"/>.</summary>
    [Fact]
    public void MultiplicityBoundaries_AreCheckedWithoutLimitingTotalCountToInt32()
    {
        var first = PersistentHashBag<int>.Empty.AddCopies(1, int.MaxValue);
        var both = first.AddCopies(2, int.MaxValue);
        var root = first.RootForTesting;

        Assert.Equal(int.MaxValue, first.CountOf(1));
        Assert.Equal(int.MaxValue, both.CountOf(2));
        Assert.Equal(2, both.DistinctCount);
        Assert.Equal(2L * int.MaxValue, both.TotalCount);

        Assert.Throws<OverflowException>(() => first.Add(1));
        Assert.Throws<OverflowException>(() => first.AddCopies(1, 1));
        Assert.Same(root, first.RootForTesting);
        Assert.Equal(int.MaxValue, first.CountOf(1));
        Assert.Equal((long)int.MaxValue, first.TotalCount);

        var removed = both.RemoveCopies(1, int.MaxValue);
        Assert.False(removed.Contains(1));
        Assert.Equal(int.MaxValue, removed.TotalCount);
        Assert.Equal(int.MaxValue, both.CountOf(1));
    }

    /// <summary>Verifies expanded array conversion uses bag order and rejects impossible array lengths eagerly.</summary>
    [Fact]
    public void ToArray_UsesExpandedOrderAndChecksArrayMaxLengthBeforeEnumeration()
    {
        var bag = PersistentHashBag<string>.Empty
            .AddCopies("alpha", 3)
            .Add("beta")
            .AddCopies("gamma", 2);
        var expected = bag.Entries
            .SelectMany(entry => Enumerable.Repeat(entry.Key, entry.Value))
            .ToArray();

        Assert.Equal(expected, bag.ToArray());
        Assert.Empty(PersistentHashBag<string>.Empty.ToArray());

        var impossibleLength = checked(Array.MaxLength + 1);
        var tooLarge = PersistentHashBag<int>.Empty.AddCopies(42, impossibleLength);
        Assert.True(tooLarge.TotalCount > Array.MaxLength);
        Assert.Throws<OverflowException>(() => tooLarge.ToArray());
        Assert.Equal(impossibleLength, tooLarge.CountOf(42));
    }

    /// <summary>Verifies comparer failures cannot publish or alter a source bag version.</summary>
    [Fact]
    public void ComparerFailures_LeavePointUpdateSourcesUnchanged()
    {
        var comparer = new SwitchableThrowingComparer();
        var bag = PersistentHashBag<string>.Create(comparer)
            .AddCopies("alpha", 2)
            .Add("beta");
        var expected = bag.Entries.ToArray();
        var expectedRoot = bag.RootForTesting;

        comparer.ThrowFromGetHashCode = true;
        var hashFailure = Assert.Throws<ComparerCallbackException>(() => bag.Add("gamma"));
        Assert.Same(comparer.Failure, hashFailure);

        comparer.ThrowFromGetHashCode = false;
        comparer.ThrowFromEquals = true;
        var equalityFailure = Assert.Throws<ComparerCallbackException>(() => bag.Remove("gamma"));
        Assert.Same(comparer.Failure, equalityFailure);

        comparer.ThrowFromEquals = false;
        Assert.Same(expectedRoot, bag.RootForTesting);
        Assert.Equal(expected, bag.Entries.ToArray());
        Assert.Equal(2, bag.CountOf("alpha"));
        Assert.Equal(1, bag.CountOf("beta"));
        bag.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies construction validates null and propagates input-enumerator failures exactly.</summary>
    [Fact]
    public void CreateRange_RejectsNullAndPropagatesInputEnumeratorFailures()
    {
        Assert.Throws<ArgumentNullException>(
            () => PersistentHashBag<string>.CreateRange(null!));

        var failure = new InputEnumerationException();
        var actual = Assert.Throws<InputEnumerationException>(
            () => PersistentHashBag<string>.CreateRange(ThrowAfterTwoItems(failure)));

        Assert.Same(failure, actual);
    }

    /// <summary>Verifies clear preserves comparer identity and canonicalizes only default-comparer emptiness.</summary>
    [Fact]
    public void Clear_PreservesComparerAndEmptyIdentityRules()
    {
        var comparer = new OrdinalComparer();
        var custom = PersistentHashBag<string>.Create(comparer).Add("value");
        var customEmpty = custom.Clear();

        Assert.Same(comparer, customEmpty.Comparer);
        Assert.NotSame(PersistentHashBag<string>.Empty, customEmpty);
        Assert.Same(customEmpty, customEmpty.Clear());

        var defaultEmpty = PersistentHashBag<string>.Empty.Add("value").Clear();
        Assert.Same(PersistentHashBag<string>.Empty, defaultEmpty);
    }

    /// <summary>Verifies bag diagnostics report valid totals and reject malformed multiplicity state.</summary>
    [Fact]
    public void CanonicalityDiagnostics_ValidateCountsAndCheckedTotal()
    {
        var comparer = new CountingConstantIgnoreCaseComparer();
        var valid = PersistentHashBag<string>.Create(comparer)
            .AddCopies("alpha", 3)
            .AddCopies("beta", 2)
            .Add("gamma");

        var diagnostics = valid.ValidateCanonicalityForDiagnostics();
        Assert.Equal(3, diagnostics.DistinctCount);
        Assert.Equal(6, diagnostics.TotalCount);
        Assert.True(diagnostics.NodeCount > 0);

        var nonpositiveCounts = PersistentHashMap<string, int>.Empty.SetItem("bad", 0);
        var nonpositive = WrapForDiagnostics(nonpositiveCounts, totalCount: 0);
        var multiplicityFailure = Assert.Throws<InvalidOperationException>(
            () => nonpositive.ValidateCanonicalityForDiagnostics());
        Assert.Contains("nonpositive multiplicity", multiplicityFailure.Message, StringComparison.Ordinal);

        var negativeCounts = PersistentHashMap<string, int>.Empty.SetItem("bad", -1);
        var negative = WrapForDiagnostics(negativeCounts, totalCount: -1);
        Assert.Throws<InvalidOperationException>(() => negative.ValidateCanonicalityForDiagnostics());

        var validCounts = PersistentHashMap<string, int>.Empty.SetItem("two", 2);
        var wrongTotal = WrapForDiagnostics(validCounts, totalCount: 1);
        var totalFailure = Assert.Throws<InvalidOperationException>(
            () => wrongTotal.ValidateCanonicalityForDiagnostics());
        Assert.Contains("differs from", totalFailure.Message, StringComparison.Ordinal);
    }

    private static PersistentHashBag<T> WrapForDiagnostics<T>(
        PersistentHashMap<T, int> counts,
        long totalCount)
    {
        var constructor = typeof(PersistentHashBag<T>).GetConstructor(
            BindingFlags.Instance | BindingFlags.NonPublic,
            binder: null,
            [typeof(PersistentHashMap<T, int>), typeof(long)],
            modifiers: null);
        Assert.NotNull(constructor);
        return (PersistentHashBag<T>)constructor.Invoke([counts, totalCount]);
    }

    private static IEnumerable<string> ThrowAfterTwoItems(Exception failure)
    {
        yield return "alpha";
        yield return "beta";
        throw failure;
    }

    private static string NewString(string value) => new(value.ToCharArray());

    private sealed class OrdinalComparer : IEqualityComparer<string>
    {
        public bool Equals(string? left, string? right) => StringComparer.Ordinal.Equals(left, right);

        public int GetHashCode(string value) => StringComparer.Ordinal.GetHashCode(value);
    }

    private sealed class CountingConstantIgnoreCaseComparer : IEqualityComparer<string>
    {
        internal int HashCalls { get; private set; }

        public bool Equals(string? left, string? right) =>
            StringComparer.OrdinalIgnoreCase.Equals(left, right);

        public int GetHashCode(string value)
        {
            HashCalls++;
            return 0;
        }
    }

    private sealed class SwitchableThrowingComparer : IEqualityComparer<string>
    {
        internal ComparerCallbackException Failure { get; } = new();

        internal bool ThrowFromEquals { get; set; }

        internal bool ThrowFromGetHashCode { get; set; }

        internal int HashCalls { get; private set; }

        public bool Equals(string? left, string? right)
        {
            if (ThrowFromEquals)
                throw Failure;
            return StringComparer.Ordinal.Equals(left, right);
        }

        public int GetHashCode(string value)
        {
            HashCalls++;
            if (ThrowFromGetHashCode)
                throw Failure;
            return 0;
        }
    }

    private sealed class ComparerCallbackException : Exception
    {
    }

    private sealed class InputEnumerationException : Exception
    {
    }
}
