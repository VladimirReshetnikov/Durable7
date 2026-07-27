// Tests for the persistent hash map transient.

using System.Collections.Concurrent;
using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Public lifecycle and mutation contract tests for CHAMP map transients.</summary>
public sealed class PersistentHashMapTransientTests
{
    /// <summary>Verifies O(1) factory/adoption semantics, comparer retention, and clean identity.</summary>
    [Fact]
    public void Factories_PreserveComparerAndCleanPublicationIdentity()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var source = PersistentHashMap<string, int>.Create(comparer).SetItem("Alpha", 1);

        var adopted = source.ToTransient();
        Assert.Same(comparer, adopted.Comparer);
        Assert.Single(adopted);
        Assert.Equal(1, adopted["ALPHA"]);
        Assert.Same(source, adopted.Persist());

        var defaultEmpty = PersistentHashMap<string, int>.CreateTransient();
        Assert.Same(PersistentHashMap<string, int>.Empty, defaultEmpty.Persist());

        var customEmpty = PersistentHashMap<string, int>.CreateTransient(comparer);
        var publishedCustomEmpty = customEmpty.Persist();
        Assert.Empty(publishedCustomEmpty);
        Assert.Same(comparer, publishedCustomEmpty.Comparer);
        Assert.NotSame(PersistentHashMap<string, int>.Empty, publishedCustomEmpty);
    }

    /// <summary>Verifies the complete point-edit vocabulary and representative retention.</summary>
    [Fact]
    public void PointEdits_MatchPersistentSemanticsAndRetainRepresentatives()
    {
        var storedKey = new string(['A', 'l', 'p', 'h', 'a']);
        var equalKey = new string(['A', 'L', 'P', 'H', 'A']);
        var storedValue = new EquatableValue("one");
        var equalValue = new EquatableValue("one");
        var changedValue = new EquatableValue("uno");
        var transient = PersistentHashMap<string, EquatableValue>
            .CreateTransient(StringComparer.OrdinalIgnoreCase);

        Assert.True(transient.TryAdd(storedKey, storedValue));
        Assert.False(transient.TryAdd(equalKey, changedValue));
        Assert.Throws<ArgumentException>(() => transient.Add(equalKey, changedValue));
        transient.SetItem(equalKey, equalValue);

        Assert.True(transient.TryGetKey(equalKey, out var actualKey));
        Assert.Same(storedKey, actualKey);
        Assert.Same(storedValue, transient[equalKey]);

        transient.SetItem(equalKey, changedValue);
        Assert.Same(changedValue, transient[storedKey]);
        Assert.True(transient.ContainsKey(equalKey));
        Assert.True(transient.Remove(equalKey));
        Assert.False(transient.Remove(equalKey));
        Assert.Throws<KeyNotFoundException>(() => transient[equalKey]);

        transient.Add("beta", new EquatableValue("two"));
        transient.Clear();
        transient.Clear();
        Assert.Empty(transient);

        var published = transient.Persist();
        Assert.Empty(published);
        Assert.Same(StringComparer.OrdinalIgnoreCase, published.Comparer);
    }

    /// <summary>Verifies null keys and null values use the configured comparer and ordinary map rules.</summary>
    [Fact]
    public void NullKeysAndValues_AreSupported()
    {
        var transient = PersistentHashMap<string?, string?>.CreateTransient();

        transient.Add(null, null);
        transient.SetItem("value", null);

        Assert.True(transient.ContainsKey(null));
        Assert.True(transient.TryGetValue(null, out var nullValue));
        Assert.Null(nullValue);
        Assert.True(transient.TryGetKey(null, out var actualNull));
        Assert.Null(actualNull);

        var published = transient.Persist();
        Assert.True(published.ContainsKey(null));
        Assert.Null(published["value"]);
    }

    /// <summary>Verifies edits isolate the retained base and publish canonical mixed-node CHAMP state.</summary>
    [Fact]
    public void EditedSession_IsolatesBaseAndPublishesCanonicalState()
    {
        var comparer = new CollisionComparer();
        var source = PersistentHashMap<int, string>.Create(comparer);
        for (var key = 0; key < 64; key++)
            source = source.SetItem(key, $"v{key}");

        var transient = source.ToTransient();
        for (var key = 0; key < 64; key += 2)
            Assert.True(transient.Remove(key));
        for (var key = 64; key < 96; key++)
            transient.SetItem(key, $"v{key}");

        Assert.Equal(64, source.Count);
        Assert.Equal("v0", source[0]);
        Assert.False(source.ContainsKey(80));

        var published = transient.Persist();
        Assert.Equal(64, published.Count);
        Assert.False(published.ContainsKey(0));
        Assert.Equal("v80", published[80]);
        var diagnostics = published.ValidateCanonicalityForDiagnostics();
        Assert.Equal(published.Count, diagnostics.RecursiveEntryCount);

        var next = published.ToTransient();
        next.SetItem(200, "later");
        var later = next.Persist();
        Assert.False(published.ContainsKey(200));
        Assert.Equal("later", later[200]);
    }

    /// <summary>Verifies the retained immutable base remains safe while its transient owner edits.</summary>
    [Fact]
    public async Task RetainedBase_RemainsReadableWhileTransientOwnerEdits()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 256).Select(key => KeyValuePair.Create(key, key * 3)));
        var transient = source.ToTransient();
        var stop = 0;
        var failures = new ConcurrentQueue<Exception>();
        using var started = new ManualResetEventSlim();
        var reader = Task.Run(() =>
        {
            try
            {
                started.Set();
                while (Volatile.Read(ref stop) == 0)
                {
                    Assert.Equal(256, source.Count);
                    for (var key = 0; key < 256; key += 17)
                        Assert.Equal(key * 3, source[key]);
                }
            }
            catch (Exception exception)
            {
                failures.Enqueue(exception);
            }
        });
        started.Wait();

        try
        {
            for (var key = 0; key < 256; key++)
                transient.SetItem(key, -key);
            for (var key = 0; key < 128; key++)
                transient.Remove(key);
        }
        finally
        {
            Volatile.Write(ref stop, 1);
        }

        await reader;
        Assert.Empty(failures);
        Assert.Equal(256, source.Count);
        Assert.Equal(128, transient.Persist().Count);
    }

    /// <summary>Verifies public operations against a deterministic dictionary model history.</summary>
    [Fact]
    public void DeterministicHistory_MatchesDictionaryModelAcrossPublications()
    {
        var random = new Random(0x4d415054);
        var model = new Dictionary<int, int>();
        var persistent = PersistentHashMap<int, int>.Empty;

        for (var epoch = 0; epoch < 12; epoch++)
        {
            var transient = persistent.ToTransient();
            for (var operation = 0; operation < 100; operation++)
            {
                var key = random.Next(48);
                switch (random.Next(4))
                {
                    case 0:
                    case 1:
                        var value = random.Next(20);
                        transient.SetItem(key, value);
                        model[key] = value;
                        break;
                    case 2:
                        Assert.Equal(model.Remove(key), transient.Remove(key));
                        break;
                    default:
                        var expectedAdded = model.TryAdd(key, random.Next(20));
                        var candidate = model.TryGetValue(key, out var existing) ? existing : default;
                        if (expectedAdded)
                            candidate = model[key];
                        Assert.Equal(expectedAdded, transient.TryAdd(key, candidate));
                        break;
                }
            }

            persistent = transient.Persist();
            Assert.Equal(
                model.OrderBy(pair => pair.Key),
                persistent.OrderBy(pair => pair.Key));
        }
    }

    /// <summary>Verifies public publication failure leaves the session active and retryable.</summary>
    [Fact]
    public void PublicationFailure_IsAtomicAndRetryableThroughPublicPersist()
    {
        var source = PersistentHashMap<int, int>.Empty.SetItem(1, 1);
        var transient = source.CreateSeparateNodeTransientKernel();
        transient.SetItem(2, 2);
        transient.SetItem(3, 3);
        var enumerator = transient.GetEnumerator();
        var version = transient.VersionForDiagnostics;
        var root = transient.RootIdentityForDiagnostics;
        transient.FailureInjector = point =>
        {
            if (point == PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforePublicationAllocation)
                throw new InjectedFailureException();
        };

        Assert.Throws<InjectedFailureException>(transient.Persist);
        Assert.True(transient.IsActiveForDiagnostics);
        Assert.Equal(version, transient.VersionForDiagnostics);
        Assert.Same(root, transient.RootIdentityForDiagnostics);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(3, transient.Count);

        transient.FailureInjector = null;
        var published = transient.Persist();
        Assert.Equal(new[] { 1, 2, 3 }, published.Keys.OrderBy(key => key));
    }

    /// <summary>Verifies hash callback failures preserve contents and enumerator validity.</summary>
    [Fact]
    public void CallbackFailure_IsAtomicThroughPublicMutation()
    {
        var comparer = new ThrowingComparer();
        var transient = PersistentHashMap<int, int>.CreateTransient(comparer);
        transient.SetItem(1, 10);
        var enumerator = transient.GetEnumerator();
        comparer.Throw = true;

        Assert.Throws<InjectedFailureException>(() => transient.SetItem(2, 20));

        comparer.Throw = false;
        Assert.True(enumerator.MoveNext());
        Assert.Equal(new KeyValuePair<int, int>(1, 10), enumerator.Current);
        Assert.Single(transient);
        Assert.False(transient.ContainsKey(2));
    }

    /// <summary>Verifies every direct and interface alias is consumed by successful publication.</summary>
    [Fact]
    public void Persist_ConsumesEveryAliasAndPreviouslyObtainedView()
    {
        var transient = PersistentHashMap<int, string>.Empty.SetItem(1, "one").ToTransient();
        IReadOnlyDictionary<int, string> dictionary = transient;
        IEnumerable<KeyValuePair<int, string>> sequence = transient;
        var keys = transient.Keys;
        var values = transient.Values;
        var pairEnumerator = transient.GetEnumerator();
        var copiedPairEnumerator = pairEnumerator;
        var keyEnumerator = keys.GetEnumerator();
        var valueEnumerator = values.GetEnumerator();

        var published = transient.Persist();
        Assert.Equal("one", published[1]);

        Assert.Throws<ObjectDisposedException>(() => _ = transient.Count);
        Assert.Throws<ObjectDisposedException>(() => _ = transient.Comparer);
        Assert.Throws<ObjectDisposedException>(() => _ = transient[1]);
        Assert.Throws<ObjectDisposedException>(() => _ = transient.Keys);
        Assert.Throws<ObjectDisposedException>(() => _ = transient.Values);
        Assert.Throws<ObjectDisposedException>(() => transient.ContainsKey(1));
        Assert.Throws<ObjectDisposedException>(() => transient.TryGetValue(1, out _));
        Assert.Throws<ObjectDisposedException>(() => transient.TryGetKey(1, out _));
        Assert.Throws<ObjectDisposedException>(() => transient.Add(2, "two"));
        Assert.Throws<ObjectDisposedException>(() => transient.TryAdd(2, "two"));
        Assert.Throws<ObjectDisposedException>(() => transient.SetItem(2, "two"));
        Assert.Throws<ObjectDisposedException>(() => transient.Remove(1));
        Assert.Throws<ObjectDisposedException>(transient.Clear);
        Assert.Throws<ObjectDisposedException>(transient.Persist);
        Assert.Throws<ObjectDisposedException>(() => transient.GetEnumerator());
        Assert.Throws<ObjectDisposedException>(() => _ = dictionary.Count);
        Assert.Throws<ObjectDisposedException>(() => sequence.GetEnumerator());
        Assert.Throws<ObjectDisposedException>(() => keys.GetEnumerator());
        Assert.Throws<ObjectDisposedException>(() => values.GetEnumerator());
        Assert.Throws<ObjectDisposedException>(() => pairEnumerator.MoveNext());
        Assert.Throws<ObjectDisposedException>(() => _ = copiedPairEnumerator.Current);
        Assert.Throws<ObjectDisposedException>(() => keyEnumerator.MoveNext());
        Assert.Throws<ObjectDisposedException>(() => _ = valueEnumerator.Current);
        Assert.Throws<ObjectDisposedException>(() => ((System.Collections.IEnumerator)pairEnumerator).Reset());
    }

    private sealed record EquatableValue(string Text);

    private sealed class CollisionComparer : IEqualityComparer<int>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(int x, int y) => x == y;

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(int obj) => obj & 3;
    }

    private sealed class ThrowingComparer : IEqualityComparer<int>
    {
        /// <summary>
        /// Throws on demand, so a test can check that a failing callback leaves the collection unchanged.
        /// </summary>
        public bool Throw { get; set; }

        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(int x, int y)
        {
            if (Throw)
                throw new InjectedFailureException();
            return x == y;
        }

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(int obj)
        {
            if (Throw)
                throw new InjectedFailureException();
            return obj;
        }
    }

    private sealed class InjectedFailureException : Exception;
}
