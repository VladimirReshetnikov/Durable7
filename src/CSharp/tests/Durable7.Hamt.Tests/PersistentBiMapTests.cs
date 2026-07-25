using System.Collections;
using System.Reflection;
using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Contract, model, failure, inverse-view, and API tests for <see cref="PersistentBiMap{TKey,TValue}"/>.</summary>
public sealed class PersistentBiMapTests
{
    /// <summary>Verifies empty factories and independently retained policies.</summary>
    [Fact]
    public void Create_PreservesIndependentComparers()
    {
        Assert.Same(PersistentBiMap<string, string>.Empty, PersistentBiMap<string, string>.Create());

        var keys = StringComparer.OrdinalIgnoreCase;
        var values = StringComparer.InvariantCultureIgnoreCase;
        var map = PersistentBiMap<string, string>.Create(keys, values);

        Assert.True(map.IsEmpty);
        Assert.Empty(map);
        Assert.Same(keys, map.KeyComparer);
        Assert.Same(values, map.ValueComparer);
        Assert.True(map.ValidateBijection());
    }

    /// <summary>Verifies symmetric lookup and cached inverse identity.</summary>
    [Fact]
    public void Add_LooksUpBothDirections_AndInverseIsCached()
    {
        var map = PersistentBiMap<string, int>.Empty.Add("one", 1).Add("two", 2);

        Assert.Equal(1, map["one"]);
        Assert.True(map.TryGetValue("two", out var two));
        Assert.Equal(2, two);
        Assert.True(map.TryGetKey(1, out var one));
        Assert.Same("one", one);
        Assert.True(map.ContainsKey("two"));
        Assert.True(map.ContainsValue(2));

        var inverse = map.Inverse;
        Assert.Same(inverse, map.Inverse);
        Assert.Same(map, inverse.Inverse);
        Assert.Equal("two", inverse[2]);
        Assert.Same(map.KeyComparer, inverse.ValueComparer);
        Assert.Same(map.ValueComparer, inverse.KeyComparer);
        Assert.True(inverse.ValidateBijection());
    }

    /// <summary>Verifies strict duplicate rejection on either equivalence domain.</summary>
    [Fact]
    public void Add_RejectsEquivalentKeyOrValue()
    {
        var map = PersistentBiMap<string, string>
            .Create(StringComparer.OrdinalIgnoreCase, StringComparer.OrdinalIgnoreCase)
            .Add("Key", "Value");

        Assert.Equal("key", Assert.Throws<ArgumentException>(() => map.Add("key", "other")).ParamName);
        Assert.Equal("value", Assert.Throws<ArgumentException>(() => map.Add("other", "value")).ParamName);
        Assert.True(map.ValidateBijection());
    }

    /// <summary>Verifies first representatives and comparer-based no-op identity.</summary>
    [Fact]
    public void SetItem_EquivalentPairPreservesRepresentativesAndIdentity()
    {
        var key = new Token("key", "stored-key");
        var value = new Token("value", "stored-value");
        var map = PersistentBiMap<Token, Token>.Create(TokenClassComparer.Instance, TokenClassComparer.Instance)
            .Add(key, value);

        var same = map.SetItem(new Token("KEY", "caller-key"), new Token("VALUE", "caller-value"));

        Assert.Same(map, same);
        Assert.True(same.TryGetKey(new Token("value", "probe"), out var storedKey));
        Assert.Same(key, storedKey);
        Assert.True(same.TryGetValue(new Token("key", "probe"), out var storedValue));
        Assert.Same(value, storedValue);
    }

    /// <summary>Guards against accidentally using the HAMT's default value equality for replacement.</summary>
    [Fact]
    public void SetItem_UsesConfiguredValueComparer_NotDefaultValueEquality()
    {
        var first = new LooseValue(7, "first");
        var second = new LooseValue(7, "second");
        Assert.Equal(first, second); // The substrate's default value equality deliberately conflates them.
        var map = PersistentBiMap<int, LooseValue>.Create(valueComparer: ExactLooseValueComparer.Instance)
            .Add(1, first);

        var changed = map.SetItem(1, second);

        Assert.NotSame(map, changed);
        Assert.Same(second, changed[1]);
        Assert.False(changed.ContainsValue(first));
        Assert.True(changed.ContainsValue(second));
        Assert.True(changed.ValidateBijection());
        Assert.Same(first, map[1]);
    }

    /// <summary>Verifies replacement retains the stored key and rejects a claimed value atomically.</summary>
    [Fact]
    public void SetItem_RetainsKeyAndRejectsOwnedValue()
    {
        var storedKey = new Token("a", "stored");
        var map = PersistentBiMap<Token, string>.Create(TokenClassComparer.Instance)
            .Add(storedKey, "one")
            .Add(new Token("b", "stored-b"), "two");

        var changed = map.SetItem(new Token("A", "caller"), "three");
        Assert.True(changed.TryGetKey("three", out var retained));
        Assert.Same(storedKey, retained);
        Assert.False(changed.ContainsValue("one"));
        Assert.Throws<ArgumentException>(() => map.SetItem(new Token("a", "caller"), "two"));
        Assert.Equal("one", map[storedKey]);
        Assert.True(map.ValidateBijection());
    }

    /// <summary>Verifies nonthrowing add and exact receiver identity on conflicts.</summary>
    [Fact]
    public void TryAdd_ReturnsReceiverOnEitherConflict()
    {
        var map = PersistentBiMap<int, string>.Empty.Add(1, "one");

        Assert.False(map.TryAdd(1, "uno", out var keyConflict));
        Assert.Same(map, keyConflict);
        Assert.False(map.TryAdd(2, "one", out var valueConflict));
        Assert.Same(map, valueConflict);
        Assert.True(map.TryAdd(2, "two", out var changed));
        Assert.Equal(2, changed.Count);
        Assert.True(changed.ValidateBijection());
    }

    /// <summary>Verifies symmetric removal results and absent no-op identity.</summary>
    [Fact]
    public void RemoveByEitherSide_IsSymmetricAndPersistent()
    {
        var map = PersistentBiMap<int, string>.Empty.Add(1, "one").Add(2, "two");

        Assert.False(map.TryRemoveKey(9, out var absentKey, out _));
        Assert.Same(map, absentKey);
        Assert.False(map.TryRemoveValue("nine", out var absentValue, out _));
        Assert.Same(map, absentValue);
        Assert.Same(map, map.RemoveKey(9));
        Assert.Same(map, map.RemoveValue("nine"));

        Assert.True(map.TryRemoveKey(1, out var withoutOne, out var removedValue));
        Assert.Equal("one", removedValue);
        Assert.False(withoutOne.ContainsValue("one"));
        Assert.True(withoutOne.TryRemoveValue("two", out var empty, out var removedKey));
        Assert.Equal(2, removedKey);
        Assert.True(empty.IsEmpty);
        Assert.Equal(2, map.Count);
        Assert.True(map.ValidateBijection());
    }

    /// <summary>Verifies clear identity and policy retention.</summary>
    [Fact]
    public void Clear_PreservesPoliciesAndEmptyIdentity()
    {
        var map = PersistentBiMap<string, string>
            .Create(StringComparer.OrdinalIgnoreCase, StringComparer.InvariantCultureIgnoreCase)
            .Add("a", "b");

        var empty = map.Clear();
        Assert.True(empty.IsEmpty);
        Assert.Same(map.KeyComparer, empty.KeyComparer);
        Assert.Same(map.ValueComparer, empty.ValueComparer);
        Assert.Same(empty, empty.Clear());
    }

    /// <summary>Verifies construction enumerates once and rejects duplicates on both sides.</summary>
    [Fact]
    public void CreateRange_IsStrictAndSinglePass()
    {
        var enumerations = 0;
        IEnumerable<KeyValuePair<int, string>> Items()
        {
            enumerations++;
            yield return KeyValuePair.Create(1, "one");
            yield return KeyValuePair.Create(2, "two");
        }

        var map = PersistentBiMap<int, string>.CreateRange(Items());
        Assert.Equal(1, enumerations);
        Assert.Equal(2, map.Count);
        Assert.Throws<ArgumentException>(() => PersistentBiMap<int, string>.CreateRange(
            [KeyValuePair.Create(1, "one"), KeyValuePair.Create(1, "two")]));
        Assert.Throws<ArgumentException>(() => PersistentBiMap<int, string>.CreateRange(
            [KeyValuePair.Create(1, "one"), KeyValuePair.Create(2, "one")]));
        Assert.Throws<ArgumentNullException>(() => PersistentBiMap<int, string>.CreateRange(null!));
    }

    /// <summary>Verifies nullable representatives remain presence-safe in both directions.</summary>
    [Fact]
    public void NullableKeysAndValues_ArePresenceSafe()
    {
        var map = PersistentBiMap<string?, string?>.Empty.Add(null, "null-key").Add("null-value", null);

        Assert.True(map.TryGetValue(null, out var value));
        Assert.Equal("null-key", value);
        Assert.True(map.TryGetKey(null, out var key));
        Assert.Equal("null-value", key);
        Assert.True(map.ValidateBijection());
    }

    /// <summary>Verifies concrete and interface enumeration and copy independence.</summary>
    [Fact]
    public void Enumeration_UsesForwardPairsAndCopiedStateIsIndependent()
    {
        var map = PersistentBiMap<int, string>.Empty.Add(1, "one").Add(2, "two").Add(3, "three");
        Assert.Equal(map.Keys.Zip(map.Values), map.Select(pair => (pair.Key, pair.Value)));

        var first = map.GetEnumerator();
        var copy = first;
        Assert.True(first.MoveNext());
        Assert.True(copy.MoveNext());
        Assert.Equal(first.Current, copy.Current);
        Assert.Equal(map.OrderBy(pair => pair.Key), ((IEnumerable<KeyValuePair<int, string>>)map).OrderBy(pair => pair.Key));
        Assert.Throws<NotSupportedException>(() => ((IEnumerator)map.GetEnumerator()).Reset());
    }

    /// <summary>Verifies comparer exceptions publish neither half of a successor.</summary>
    [Fact]
    public void ComparerFailure_LeavesSourceAndCachedInverseUntouched()
    {
        var keys = new ThrowingComparer<int>();
        var values = new ThrowingComparer<string>();
        var map = PersistentBiMap<int, string>.Create(keys, values).Add(1, "one");
        var inverse = map.Inverse;
        values.Throw = true;

        Assert.Throws<InvalidOperationException>(() => map.Add(2, "two"));
        values.Throw = false;
        Assert.Single(map);
        Assert.Same(inverse, map.Inverse);
        Assert.Same(map, inverse.Inverse);
        Assert.True(map.ValidateBijection());
    }

    /// <summary>Runs deterministic retained-version histories against independent dictionaries.</summary>
    [Fact]
    public void GeneratedHistory_MatchesBidirectionalModel()
    {
        var random = new Random(0xB1A4);
        var actual = PersistentBiMap<int, int>.Empty;
        var forward = new Dictionary<int, int>();
        var inverse = new Dictionary<int, int>();
        var retained = new List<(PersistentBiMap<int, int> Map, Dictionary<int, int> Model)>();

        for (var step = 0; step < 1_000; step++)
        {
            if (step % 37 == 0)
                retained.Add((actual, new(forward)));
            var key = random.Next(24);
            var value = random.Next(24);
            switch (random.Next(4))
            {
                case 0:
                    var canAdd = !forward.ContainsKey(key) && !inverse.ContainsKey(value);
                    Assert.Equal(canAdd, actual.TryAdd(key, value, out var added));
                    actual = added;
                    if (canAdd)
                    {
                        forward.Add(key, value);
                        inverse.Add(value, key);
                    }
                    break;
                case 1:
                    if (!inverse.TryGetValue(value, out var owner) || owner == key)
                    {
                        if (forward.TryGetValue(key, out var oldValue))
                            inverse.Remove(oldValue);
                        forward[key] = value;
                        inverse[value] = key;
                        actual = actual.SetItem(key, value);
                    }
                    else
                    {
                        Assert.Throws<ArgumentException>(() => actual.SetItem(key, value));
                    }
                    break;
                case 2:
                    if (forward.Remove(key, out var removedValue))
                        inverse.Remove(removedValue);
                    actual = actual.RemoveKey(key);
                    break;
                default:
                    if (inverse.Remove(value, out var removedKey))
                        forward.Remove(removedKey);
                    actual = actual.RemoveValue(value);
                    break;
            }

            AssertModel(actual, forward);
        }

        foreach (var (map, model) in retained)
            AssertModel(map, model);
    }

    /// <summary>Verifies concurrent readers and inverse publication over one immutable snapshot.</summary>
    [Fact]
    public void RetainedSnapshot_SupportsConcurrentReaders()
    {
        var map = PersistentBiMap<int, int>.CreateRange(Enumerable.Range(0, 1_000).Select(i => KeyValuePair.Create(i, -i)));

        Parallel.For(0, 8, _ =>
        {
            for (var i = 0; i < 1_000; i++)
            {
                Assert.Equal(-i, map[i]);
                Assert.Equal(i, map.Inverse[-i]);
                Assert.Same(map, map.Inverse.Inverse);
            }
        });
    }

    /// <summary>Locks the intentionally small immutable public surface.</summary>
    [Fact]
    public void ApiShape_IsClosedAndImmutable()
    {
        var type = typeof(PersistentBiMap<string, int>);
        Assert.True(type.IsSealed);
        Assert.Empty(type.GetConstructors(BindingFlags.Public | BindingFlags.Instance));
        Assert.Equal(
            new[]
            {
                "Clear", "ContainsKey", "ContainsValue", "Create", "CreateRange", "Add", "GetEnumerator",
                "RemoveKey", "RemoveValue", "SetItem", "TryAdd", "TryGetKey", "TryGetValue",
                "TryRemoveKey", "TryRemoveValue", "get_Count", "get_Empty", "get_Inverse", "get_IsEmpty",
                "get_Item", "get_KeyComparer", "get_Keys", "get_ValueComparer", "get_Values",
            }.OrderBy(name => name),
            type.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
                .Select(method => method.Name).OrderBy(name => name));
        Assert.Null(type.GetNestedType("Builder", BindingFlags.Public));
        Assert.Null(type.GetNestedType("Transient", BindingFlags.Public));
    }

    private static void AssertModel(PersistentBiMap<int, int> actual, Dictionary<int, int> expected)
    {
        Assert.Equal(expected.Count, actual.Count);
        Assert.True(actual.ValidateBijection());
        Assert.Equal(expected.OrderBy(pair => pair.Key), actual.OrderBy(pair => pair.Key));
        foreach (var (key, value) in expected)
        {
            Assert.Equal(value, actual[key]);
            Assert.Equal(key, actual.Inverse[value]);
        }
    }

    private sealed record Token(string Class, string Representative);

    private sealed class TokenClassComparer : IEqualityComparer<Token>
    {
        internal static TokenClassComparer Instance { get; } = new();

        public bool Equals(Token? x, Token? y) =>
            ReferenceEquals(x, y) || (x is not null && y is not null
                && StringComparer.OrdinalIgnoreCase.Equals(x.Class, y.Class));

        public int GetHashCode(Token obj) => StringComparer.OrdinalIgnoreCase.GetHashCode(obj.Class);
    }

    private sealed class LooseValue(int @class, string representative) : IEquatable<LooseValue>
    {
        internal int Class { get; } = @class;
        internal string Representative { get; } = representative;

        public bool Equals(LooseValue? other) => other is not null && Class == other.Class;

        public override bool Equals(object? obj) => obj is LooseValue other && Equals(other);

        public override int GetHashCode() => Class;
    }

    private sealed class ExactLooseValueComparer : IEqualityComparer<LooseValue>
    {
        internal static ExactLooseValueComparer Instance { get; } = new();

        public bool Equals(LooseValue? x, LooseValue? y) =>
            ReferenceEquals(x, y) || (x is not null && y is not null && x.Class == y.Class
                && StringComparer.Ordinal.Equals(x.Representative, y.Representative));

        public int GetHashCode(LooseValue obj) => HashCode.Combine(obj.Class, obj.Representative);
    }

    private sealed class ThrowingComparer<T> : IEqualityComparer<T>
    {
        internal bool Throw { get; set; }

        public bool Equals(T? x, T? y)
        {
            FailIfRequested();
            return EqualityComparer<T>.Default.Equals(x!, y!);
        }

        public int GetHashCode(T obj)
        {
            FailIfRequested();
            return EqualityComparer<T>.Default.GetHashCode(obj!);
        }

        private void FailIfRequested()
        {
            if (Throw)
                throw new InvalidOperationException("Injected comparer failure.");
        }
    }
}
