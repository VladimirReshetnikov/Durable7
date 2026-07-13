using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Public lifecycle, mutation, relation, and enumeration tests for CHAMP set transients.</summary>
public sealed class PersistentHashSetTransientTests
{
    /// <summary>Verifies factory/adoption comparer semantics and exact clean set identity.</summary>
    [Fact]
    public void Factories_PreserveComparerAndCleanSetIdentity()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var source = PersistentHashSet<string>.Create(comparer).Add("Alpha");
        var adopted = source.ToTransient();

        Assert.Single(adopted);
        Assert.Same(comparer, adopted.Comparer);
        Assert.True(adopted.Contains("ALPHA"));
        Assert.Same(source, adopted.Persist());

        Assert.Same(PersistentHashSet<string>.Empty, PersistentHashSet<string>.CreateTransient().Persist());
        var customEmpty = PersistentHashSet<string>.CreateTransient(comparer).Persist();
        Assert.Empty(customEmpty);
        Assert.Same(comparer, customEmpty.Comparer);
        Assert.NotSame(PersistentHashSet<string>.Empty, customEmpty);
    }

    /// <summary>Verifies mutable-set verbs and first representative retention.</summary>
    [Fact]
    public void MutableVerbs_RetainFirstRepresentativeAndReportChanges()
    {
        var stored = new string(['A', 'l', 'p', 'h', 'a']);
        var equivalent = new string(['A', 'L', 'P', 'H', 'A']);
        var transient = PersistentHashSet<string>.CreateTransient(StringComparer.OrdinalIgnoreCase);

        Assert.True(transient.Add(stored));
        Assert.False(transient.Add(equivalent));
        Assert.True(transient.TryGetValue(equivalent, out var actual));
        Assert.Same(stored, actual);
        Assert.True(transient.Contains(equivalent));
        Assert.True(transient.Remove(equivalent));
        Assert.False(transient.Remove(equivalent));

        var missing = "missing";
        Assert.False(transient.TryGetValue(missing, out var fallback));
        Assert.Same(missing, fallback);

        transient.Add("beta");
        transient.Clear();
        transient.Clear();
        Assert.Empty(transient);
        var published = transient.Persist();
        Assert.Empty(published);
        Assert.Same(StringComparer.OrdinalIgnoreCase, published.Comparer);
    }

    /// <summary>Verifies null items and equal-hash collision buckets through the set facade.</summary>
    [Fact]
    public void NullAndCollisionItems_RoundTripThroughPublication()
    {
        var transient = PersistentHashSet<string?>.CreateTransient(new NullableCollisionComparer());
        Assert.True(transient.Add(null));
        for (var index = 0; index < 40; index++)
            Assert.True(transient.Add($"item-{index}"));

        Assert.True(transient.Contains(null));
        Assert.True(transient.TryGetValue(null, out var actualNull));
        Assert.Null(actualNull);

        var published = transient.Persist();
        Assert.Equal(41, published.Count);
        Assert.True(published.Contains(null));
        var diagnostics = published.ValidateCanonicalityForDiagnostics();
        Assert.Equal(published.Count, diagnostics.RecursiveEntryCount);
    }

    /// <summary>Verifies the active facade preserves the full read-only set relation contract.</summary>
    [Fact]
    public void ReadOnlySetRelations_UseReceiverComparerSemantics()
    {
        IReadOnlySet<string> transient = PersistentHashSet<string>
            .Create(StringComparer.OrdinalIgnoreCase)
            .Add("Alpha")
            .Add("Beta")
            .ToTransient();

        Assert.True(transient.IsSubsetOf(new[] { "ALPHA", "BETA", "gamma" }));
        Assert.True(transient.IsProperSubsetOf(new[] { "ALPHA", "BETA", "gamma" }));
        Assert.True(transient.IsSupersetOf(new[] { "alpha" }));
        Assert.True(transient.IsProperSupersetOf(new[] { "alpha" }));
        Assert.True(transient.Overlaps(new[] { "none", "BETA" }));
        Assert.True(transient.SetEquals(new[] { "beta", "alpha", "ALPHA" }));
        Assert.False(transient.SetEquals(new[] { "alpha" }));
        Assert.Throws<ArgumentNullException>(() => transient.SetEquals(null!));
    }

    /// <summary>Verifies edits isolate the retained base and later transient generations.</summary>
    [Fact]
    public void EditedSession_IsolatesBaseAndLaterGenerations()
    {
        var source = PersistentHashSet<int>.CreateRange(Enumerable.Range(0, 64));
        var transient = source.ToTransient();
        for (var item = 0; item < 64; item += 2)
            transient.Remove(item);
        for (var item = 64; item < 96; item++)
            transient.Add(item);

        Assert.Equal(64, source.Count);
        Assert.True(source.Contains(0));
        Assert.False(source.Contains(80));

        var published = transient.Persist();
        Assert.Equal(64, published.Count);
        Assert.False(published.Contains(0));
        Assert.True(published.Contains(80));

        var next = published.ToTransient();
        next.Add(200);
        var later = next.Persist();
        Assert.False(published.Contains(200));
        Assert.True(later.Contains(200));
    }

    /// <summary>Verifies deterministic mutation histories against <see cref="HashSet{T}"/>.</summary>
    [Fact]
    public void DeterministicHistory_MatchesHashSetAcrossPublications()
    {
        var random = new Random(0x53455454);
        var model = new HashSet<int>();
        var persistent = PersistentHashSet<int>.Empty;

        for (var epoch = 0; epoch < 12; epoch++)
        {
            var transient = persistent.ToTransient();
            for (var operation = 0; operation < 120; operation++)
            {
                var item = random.Next(64);
                if (random.Next(2) == 0)
                    Assert.Equal(model.Add(item), transient.Add(item));
                else
                    Assert.Equal(model.Remove(item), transient.Remove(item));
            }

            persistent = transient.Persist();
            Assert.Equal(model.OrderBy(item => item), persistent.OrderBy(item => item));
        }
    }

    /// <summary>Verifies set-wrapper allocation failures do not consume the map session.</summary>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    public void SetPublicationFailure_IsAtomicAndRetryable(int failurePointValue)
    {
        var failurePoint = (PersistentHashSet<int>.TransientFailurePoint)failurePointValue;
        var source = PersistentHashSet<int>.Empty.Add(1);
        var transient = source.CreateTransientForDiagnostics();
        Assert.True(transient.Add(2));
        var map = transient.MapForDiagnostics;
        var version = map.VersionForDiagnostics;
        var root = map.RootIdentityForDiagnostics;
        var counters = map.GetCountersForDiagnostics();
        var enumerator = transient.GetEnumerator();
        transient.FailureInjector = point =>
        {
            if (point == failurePoint)
                throw new InjectedFailureException();
        };

        Assert.Throws<InjectedFailureException>(transient.Persist);
        Assert.True(map.IsActiveForDiagnostics);
        Assert.Equal(version, map.VersionForDiagnostics);
        Assert.Same(root, map.RootIdentityForDiagnostics);
        Assert.Equal(counters, map.GetCountersForDiagnostics());
        Assert.True(enumerator.MoveNext());
        Assert.Equal(2, transient.Count);

        transient.FailureInjector = null;
        var published = transient.Persist();
        Assert.Equal(new[] { 1, 2 }, published.OrderBy(item => item));
    }

    /// <summary>Verifies map-wrapper preparation failures remain atomic through the set facade.</summary>
    [Fact]
    public void MapPublicationFailure_IsAtomicThroughSetPersist()
    {
        var source = PersistentHashSet<int>.CreateRange(new[] { 1, 2, 3 });
        var transient = source.CreateTransientForDiagnostics();
        transient.Add(4);
        transient.Add(5);
        var map = transient.MapForDiagnostics;
        var version = map.VersionForDiagnostics;
        var root = map.RootIdentityForDiagnostics;
        var enumerator = transient.GetEnumerator();
        map.FailureInjector = point =>
        {
            if (point == PersistentHashMap<int, PersistentHashSet<int>.Unit>
                .OwnerTokenKernelFailurePoint.BeforePublicationAllocation)
            {
                throw new InjectedFailureException();
            }
        };

        Assert.Throws<InjectedFailureException>(transient.Persist);
        Assert.True(map.IsActiveForDiagnostics);
        Assert.Equal(version, map.VersionForDiagnostics);
        Assert.Same(root, map.RootIdentityForDiagnostics);
        Assert.True(enumerator.MoveNext());

        map.FailureInjector = null;
        Assert.Equal(new[] { 1, 2, 3, 4, 5 }, transient.Persist().OrderBy(item => item));
    }

    /// <summary>Verifies point-mutation failpoints remain atomic through the public set verb.</summary>
    [Fact]
    public void MutationFailure_IsAtomicThroughSetAdd()
    {
        var transient = PersistentHashSet<int>.Empty.Add(1).CreateTransientForDiagnostics();
        var map = transient.MapForDiagnostics;
        var version = map.VersionForDiagnostics;
        var root = map.RootIdentityForDiagnostics;
        var counters = map.GetCountersForDiagnostics();
        var enumerator = transient.GetEnumerator();
        map.FailureInjector = point =>
        {
            if (point == PersistentHashMap<int, PersistentHashSet<int>.Unit>
                .OwnerTokenKernelFailurePoint.MutationPrepared)
            {
                throw new InjectedFailureException();
            }
        };

        Assert.Throws<InjectedFailureException>(() => transient.Add(2));
        Assert.Equal(version, map.VersionForDiagnostics);
        Assert.Same(root, map.RootIdentityForDiagnostics);
        Assert.Equal(counters, map.GetCountersForDiagnostics());
        Assert.True(enumerator.MoveNext());
        Assert.False(transient.Contains(2));

        map.FailureInjector = null;
        Assert.True(transient.Add(2));
        Assert.Equal(new[] { 1, 2 }, transient.Persist().OrderBy(item => item));
    }

    /// <summary>Verifies successful changes invalidate enumerators while logical no-ops do not.</summary>
    [Fact]
    public void Enumerators_AreVersionBoundAndCopySafe()
    {
        var transient = PersistentHashSet<int>.Empty.Add(1).Add(2).ToTransient();
        var original = transient.GetEnumerator();
        var copy = original;
        Assert.False(transient.Add(1));
        Assert.False(transient.Remove(99));
        Assert.True(original.MoveNext());
        Assert.True(copy.MoveNext());
        Assert.Equal(original.Current, copy.Current);

        transient.Add(3);
        Assert.Throws<InvalidOperationException>(() => original.MoveNext());
        Assert.Throws<InvalidOperationException>(() => _ = copy.Current);

        var empty = PersistentHashSet<int>.CreateTransient();
        var emptyEnumerator = empty.GetEnumerator();
        empty.Clear();
        Assert.False(emptyEnumerator.MoveNext());
    }

    /// <summary>Verifies publication consumes direct, interface, relation, and enumerator aliases.</summary>
    [Fact]
    public void Persist_ConsumesEveryPublicSetAlias()
    {
        var transient = PersistentHashSet<int>.Empty.Add(1).ToTransient();
        IReadOnlySet<int> set = transient;
        IEnumerable<int> sequence = transient;
        var enumerator = transient.GetEnumerator();
        var copiedEnumerator = enumerator;
        var interfaceEnumerator = sequence.GetEnumerator();
        var published = transient.Persist();
        Assert.Contains(1, published);

        Assert.Throws<ObjectDisposedException>(() => _ = transient.Count);
        Assert.Throws<ObjectDisposedException>(() => _ = transient.Comparer);
        Assert.Throws<ObjectDisposedException>(() => transient.Contains(1));
        Assert.Throws<ObjectDisposedException>(() => transient.TryGetValue(1, out _));
        Assert.Throws<ObjectDisposedException>(() => transient.Add(2));
        Assert.Throws<ObjectDisposedException>(() => transient.Remove(1));
        Assert.Throws<ObjectDisposedException>(transient.Clear);
        Assert.Throws<ObjectDisposedException>(transient.Persist);
        Assert.Throws<ObjectDisposedException>(() => transient.GetEnumerator());
        Assert.Throws<ObjectDisposedException>(() => _ = set.Count);
        Assert.Throws<ObjectDisposedException>(() => set.IsSubsetOf(new[] { 1 }));
        Assert.Throws<ObjectDisposedException>(() => set.IsProperSubsetOf(new[] { 1, 2 }));
        Assert.Throws<ObjectDisposedException>(() => set.IsSupersetOf(new[] { 1 }));
        Assert.Throws<ObjectDisposedException>(() => set.IsProperSupersetOf(Array.Empty<int>()));
        Assert.Throws<ObjectDisposedException>(() => set.Overlaps(new[] { 1 }));
        Assert.Throws<ObjectDisposedException>(() => set.SetEquals(new[] { 1 }));
        Assert.Throws<ObjectDisposedException>(() => sequence.GetEnumerator());
        Assert.Throws<ObjectDisposedException>(() => enumerator.MoveNext());
        Assert.Throws<ObjectDisposedException>(() => _ = copiedEnumerator.Current);
        Assert.Throws<ObjectDisposedException>(() => interfaceEnumerator.MoveNext());
        Assert.Throws<ObjectDisposedException>(() => ((System.Collections.IEnumerator)enumerator).Reset());
    }

    /// <summary>Verifies the default set transient enumerator is exhausted.</summary>
    [Fact]
    public void DefaultEnumerator_IsExhausted()
    {
        var enumerator = default(PersistentHashSet<int>.Transient.Enumerator);
        Assert.Equal(default, enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.Throws<NotSupportedException>(() => ((System.Collections.IEnumerator)enumerator).Reset());
    }

    private sealed class NullableCollisionComparer : IEqualityComparer<string?>
    {
        public bool Equals(string? x, string? y) => StringComparer.Ordinal.Equals(x, y);

        public int GetHashCode(string? obj) => 0;
    }

    private sealed class InjectedFailureException : Exception;
}
